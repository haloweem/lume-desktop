#include "apps/shell/diagnostic_report.h"
#include "apps/shell/resource_snapshot.h"
#include "apps/shell/self_check.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/text_util.h"

#include <windows.h>
#include <dxgi.h>
#include <psapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winevt.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <vector>

namespace lume::shell {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using lume::text::Utf8;
using Microsoft::WRL::ComPtr;

// The whole report stays small enough to send in a chat app and is built in
// memory once: a hard total, filled in order of usefulness.
constexpr std::uintmax_t MaxReportBytes = 32ull << 20;
constexpr std::uintmax_t MaxLogBytes = 4ull << 20;       // Newest part of each log.
constexpr std::uintmax_t MaxSettingBytes = 512ull << 10; // One preference file.
constexpr std::uintmax_t MaxWerBytes = 64ull << 10;      // One Report.wer.
constexpr std::uintmax_t MaxDumpBytes = 16ull << 20;     // One crash dump.
constexpr std::size_t MaxDumps = 2;
constexpr std::size_t MaxWerReports = 10;
constexpr std::size_t MaxEventRecords = 60;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
bool Contains(const std::wstring& haystack, std::wstring_view needle) { return haystack.find(needle) != std::wstring::npos; }

std::string Text(const wchar_t* value) { return value ? Utf8(value) : std::string{}; }

std::wstring RegistryText(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t value[512]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}
std::optional<DWORD> RegistryNumber(HKEY root, const wchar_t* key, const wchar_t* name) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return std::nullopt;
    return value;
}
Json Optional(const std::optional<DWORD>& value) { return value ? Json(*value) : Json(nullptr); }

std::string FileVersion(const fs::path& file) {
    DWORD handle = 0;
    const auto size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    if (!size) return {};
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return {};
    char text[64]{};
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                  HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return text;
}

std::string LocalTime(const FILETIME& value) {
    FILETIME local{};
    SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&value, &local) || !FileTimeToSystemTime(&local, &time)) return {};
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour,
                  time.wMinute, time.wSecond);
    return text;
}
std::string Now() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return LocalTime(now);
}

Json Windows() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (const auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) get(&version);
    constexpr auto key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    Json result{{"build", version.dwBuildNumber},
                {"version", std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion)},
                {"productName", Utf8(RegistryText(HKEY_LOCAL_MACHINE, key, L"ProductName"))},
                {"edition", Utf8(RegistryText(HKEY_LOCAL_MACHINE, key, L"EditionID"))},
                {"displayVersion", Utf8(RegistryText(HKEY_LOCAL_MACHINE, key, L"DisplayVersion"))},
                {"ubr", Optional(RegistryNumber(HKEY_LOCAL_MACHINE, key, L"UBR"))},
                {"windows11", version.dwBuildNumber >= 22000}};
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) result["locale"] = Utf8(locale);
    result["uptimeMinutes"] = GetTickCount64() / 60000;
    result["remoteSession"] = GetSystemMetrics(SM_REMOTESESSION) != 0;
    return result;
}

Json Hardware() {
    Json result = Json::object();
    result["cpu"] = Utf8(RegistryText(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    result["logicalProcessors"] = system.dwNumberOfProcessors;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result["memoryTotalMB"] = memory.ullTotalPhys >> 20;
        result["memoryAvailableMB"] = memory.ullAvailPhys >> 20;
        result["commitLimitMB"] = memory.ullTotalPageFile >> 20;
        result["commitAvailableMB"] = memory.ullAvailPageFile >> 20;
    }
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        result["power"] = {{"acLine", power.ACLineStatus == 1}, {"batteryPercent", power.BatteryLifePercent},
                           {"batterySaver", power.SystemStatusFlag == 1}};
    Json gpus = Json::array();
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc))) continue;
            Json gpu{{"name", Utf8(desc.Description)}, {"vendorId", desc.VendorId}, {"deviceId", desc.DeviceId},
                     {"dedicatedMB", static_cast<std::uint64_t>(desc.DedicatedVideoMemory >> 20)},
                     {"sharedMB", static_cast<std::uint64_t>(desc.SharedSystemMemory >> 20)},
                     {"software", (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0}};
            LARGE_INTEGER umd{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
                char text[48]{};
                std::snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart),
                              HIWORD(umd.LowPart), LOWORD(umd.LowPart));
                gpu["driver"] = text;
            }
            Json outputs = Json::array();
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o, output.Reset()) {
                DXGI_OUTPUT_DESC out{};
                if (SUCCEEDED(output->GetDesc(&out))) outputs.push_back(Utf8(out.DeviceName));
            }
            gpu["outputs"] = outputs;
            gpus.push_back(gpu);
        }
    }
    result["gpus"] = gpus;
    return result;
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& list = *reinterpret_cast<Json*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    Json item{{"device", Utf8(info.szDevice)}, {"primary", (info.dwFlags & MONITORINFOF_PRIMARY) != 0},
              {"bounds", {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom}},
              {"work", {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom}}};
    UINT dpiX = 0, dpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) item["scalePercent"] = dpiX * 100 / 96;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        item["mode"] = std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + "@" +
                       std::to_string(mode.dmDisplayFrequency) + "Hz";
        item["orientation"] = mode.dmDisplayOrientation;
    }
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (EnumDisplayDevicesW(info.szDevice, 0, &device, 0)) item["monitor"] = Utf8(device.DeviceString);
    list.push_back(item);
    return TRUE;
}

Json Appearance() {
    constexpr auto personalize = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    constexpr auto advanced = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    BOOL composition = FALSE;
    using DwmFn = HRESULT(WINAPI*)(BOOL*);
    if (const auto dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (const auto enabled = reinterpret_cast<DwmFn>(GetProcAddress(dwm, "DwmIsCompositionEnabled"))) enabled(&composition);
        FreeLibrary(dwm);
    }
    return {{"transparencyEffects", Optional(RegistryNumber(HKEY_CURRENT_USER, personalize, L"EnableTransparency"))},
            {"appsLightTheme", Optional(RegistryNumber(HKEY_CURRENT_USER, personalize, L"AppsUseLightTheme"))},
            {"systemLightTheme", Optional(RegistryNumber(HKEY_CURRENT_USER, personalize, L"SystemUsesLightTheme"))},
            {"animations", animations != FALSE},
            {"dwmComposition", composition != FALSE},
            {"taskbarAutoHideSetting", Optional(RegistryNumber(HKEY_CURRENT_USER, advanced, L"TaskbarAl"))},
            {"taskbarSmallIcons", Optional(RegistryNumber(HKEY_CURRENT_USER, advanced, L"TaskbarSi"))},
            {"hideDesktopIcons", Optional(RegistryNumber(HKEY_CURRENT_USER, advanced, L"HideIcons"))}};
}

// Explorer's non-Windows modules show the taskbar and desktop mods sharing
// Explorer with Lume (StartAllBack, ExplorerPatcher, Windhawk...).
Json ShellEnvironment() {
    Json result = Json::object();
    const auto tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    result["taskbarWindow"] = tray != nullptr;
    result["taskbarXamlIsland"] = tray && FindWindowExW(tray, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    result["secondaryTaskbars"] = 0;
    for (HWND window = nullptr; (window = FindWindowExW(nullptr, window, L"Shell_SecondaryTrayWnd", nullptr));)
        result["secondaryTaskbars"] = result["secondaryTaskbars"].get<int>() + 1;
    result["progman"] = FindWindowW(L"Progman", nullptr) != nullptr;
    DWORD explorer = 0;
    if (tray) GetWindowThreadProcessId(tray, &explorer);
    result["explorerPid"] = explorer;
    Json modules = Json::array();
    if (explorer) {
        const auto process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, explorer);
        if (process) {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(process, &created, &exited, &kernel, &user)) result["explorerStarted"] = LocalTime(created);
            std::vector<HMODULE> handles(1024);
            DWORD needed = 0;
            if (EnumProcessModulesEx(process, handles.data(), static_cast<DWORD>(handles.size() * sizeof(HMODULE)), &needed, LIST_MODULES_ALL)) {
                wchar_t windowsDir[MAX_PATH]{};
                GetWindowsDirectoryW(windowsDir, MAX_PATH);
                const auto system = Lower(windowsDir);
                const auto count = (std::min)(handles.size(), static_cast<std::size_t>(needed / sizeof(HMODULE)));
                for (std::size_t i = 0; i < count; ++i) {
                    wchar_t name[MAX_PATH]{};
                    if (!GetModuleFileNameExW(process, handles[i], name, MAX_PATH)) continue;
                    const auto lower = Lower(name);
                    if (lower.rfind(system, 0) == 0) continue;
                    modules.push_back(Utf8(name));
                }
            }
            CloseHandle(process);
        }
    }
    result["explorerThirdPartyModules"] = modules;
    return result;
}

// Software that changes the same desktop, wallpaper or taskbar as Lume.
Json RelatedSoftware() {
    static constexpr std::array<const wchar_t*, 22> names{
        L"translucenttb.exe", L"roundedtb.exe", L"taskbarx.exe", L"startallbackcfg.exe", L"explorerpatcher",
        L"windhawk.exe", L"rainmeter.exe", L"wallpaper32.exe", L"wallpaper64.exe", L"lively.exe",
        L"livelywpf.exe", L"mica", L"fences.exe", L"desktopok.exe", L"mydockfinder.exe", L"nexus.exe",
        L"objectdock.exe", L"rocketdock.exe", L"winstep", L"stardock", L"displayfusion.exe", L"msi afterburner"};
    Json result = Json::array();
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::set<std::wstring> seen;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        const auto name = Lower(entry.szExeFile);
        for (const auto candidate : names)
            if (Contains(name, candidate) && seen.insert(name).second) result.push_back(Utf8(entry.szExeFile));
    }
    CloseHandle(snapshot);
    return result;
}

Json Installation(const fs::path& install) {
    Json result{{"directory", install.u8string()}};
    for (const auto* name : {L"LumeShell.exe", L"wallpaper-runtime\\mpv.exe", L"LumeMusicHost.exe", L"WebView2Loader.dll"}) {
        const auto file = install / name;
        std::error_code error;
        if (!fs::is_regular_file(file, error)) { result["files"][Utf8(name)] = "missing"; continue; }
        result["files"][Utf8(name)] = {{"bytes", fs::file_size(file, error)}, {"version", FileVersion(file)}};
    }
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(install / L"taskbar-runtime", error)) {
        if (Lower(entry.path().filename().wstring()) == L"lumetaskbarcore.exe")
            result["files"][fs::relative(entry.path(), install, error).u8string()] = {
                {"bytes", entry.file_size(error)}, {"version", FileVersion(entry.path())}};
    }
    const auto webview = RegistryText(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}", L"pv");
    result["webView2Runtime"] = Utf8(webview);
    ULARGE_INTEGER free{};
    if (GetDiskFreeSpaceExW(install.c_str(), &free, nullptr, nullptr)) result["freeDiskMB"] = free.QuadPart >> 20;
    return result;
}

// Windows' own records of Lume, mpv or WebView processes crashing or hanging.
struct CrashRecords {
    std::string text;
    std::vector<std::string> rows;  // Newest first, one readable line each.
};
std::string XmlField(const std::string& xml, const std::string& name) {
    const auto key = "<Data Name='" + name + "'>";
    const auto at = xml.find(key);
    if (at == std::string::npos) return {};
    const auto from = at + key.size(), to = xml.find("</Data>", from);
    return to == std::string::npos ? std::string{} : xml.substr(from, (std::min<std::size_t>)(to - from, 200));
}
std::string XmlAttribute(const std::string& xml, const std::string& element, const std::string& attribute) {
    const auto at = xml.find("<" + element);
    if (at == std::string::npos) return {};
    const auto key = attribute + "='";
    const auto from = xml.find(key, at);
    if (from == std::string::npos) return {};
    const auto to = xml.find('\'', from + key.size());
    return to == std::string::npos ? std::string{} : xml.substr(from + key.size(), to - from - key.size());
}
std::string EventRow(const std::string& xml) {
    auto time = XmlAttribute(xml, "TimeCreated", "SystemTime");
    if (time.size() > 19) time = time.substr(0, 19);
    std::replace(time.begin(), time.end(), 'T', ' ');
    const auto provider = XmlAttribute(xml, "Provider", "Name");
    std::string row = time + " UTC  ";
    if (provider == "Application Error")
        row += "崩溃  " + XmlField(xml, "AppName") + "  模块 " + XmlField(xml, "ModuleName") + " " + XmlField(xml, "ModuleVersion") +
               "  异常 0x" + XmlField(xml, "ExceptionCode") + "  偏移 0x" + XmlField(xml, "FaultingOffset");
    else if (provider == "Application Hang")
        row += "无响应  " + XmlField(xml, "AppName") + " " + XmlField(xml, "AppVersion") + "  类型 " + XmlField(xml, "HangType");
    else {
        // Windows Error Reporting: event name, then the bucket's parameters.
        row += "WER " + XmlField(xml, "EventName");
        for (const auto* field : {"P1", "P2", "P4", "P5", "P7", "P8"})
            if (const auto value = XmlField(xml, field); !value.empty()) row += "  " + value;
    }
    return row;
}
CrashRecords CrashEvents() {
    CrashRecords records;
    std::string raw;
    const wchar_t* query =
        L"*[System[(Provider[@Name='Application Error'] or Provider[@Name='Application Hang'] or "
        L"Provider[@Name='Windows Error Reporting']) and TimeCreated[timediff(@SystemTime) <= 2592000000]]]";
    const auto results = EvtQuery(nullptr, L"Application", query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!results) {
        records.text = "Event log query failed: " + std::to_string(GetLastError()) + "\r\n";
        return records;
    }
    std::array<EVT_HANDLE, 32> events{};
    DWORD returned = 0;
    while (records.rows.size() < MaxEventRecords && EvtNext(results, static_cast<DWORD>(events.size()), events.data(), 2000, 0, &returned)) {
        for (DWORD i = 0; i < returned; ++i) {
            DWORD used = 0, properties = 0;
            EvtRender(nullptr, events[i], EvtRenderEventXml, 0, nullptr, &used, &properties);
            std::wstring xml(used / sizeof(wchar_t) + 1, L'\0');
            if (EvtRender(nullptr, events[i], EvtRenderEventXml, static_cast<DWORD>(xml.size() * sizeof(wchar_t)), xml.data(), &used, &properties)) {
                xml.resize(wcslen(xml.c_str()));
                const auto lower = Lower(xml);
                if (records.rows.size() < MaxEventRecords &&
                    (Contains(lower, L"lume") || Contains(lower, L"mpv.exe") || Contains(lower, L"msedgewebview2"))) {
                    const auto text = Utf8(xml);
                    records.rows.push_back(EventRow(text));
                    raw += text + "\r\n\r\n";
                }
            }
            EvtClose(events[i]);
        }
    }
    EvtClose(results);
    if (records.rows.empty()) {
        records.text = "No Lume, mpv or WebView2 crash or hang records in the last 30 days.\r\n";
        return records;
    }
    for (const auto& row : records.rows) records.text += row + "\r\n";
    records.text += "\r\n---- raw event records ----\r\n\r\n" + raw;
    return records;
}

struct Entry {
    std::string name;
    std::string bytes;
    FILETIME time{};
};

std::optional<std::string> ReadShared(const fs::path& file, std::uintmax_t limit) {
    // Shared read: the runtime log may be written while it is copied.
    const auto handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    std::optional<std::string> result;
    if (GetFileSizeEx(handle, &size)) {
        const auto total = static_cast<std::uintmax_t>(size.QuadPart);
        const auto start = total > limit ? total - limit : 0;  // Keep the newest part.
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(start);
        SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN);
        std::string data(static_cast<std::size_t>(total - start), '\0');
        DWORD read = 0;
        std::size_t done = 0;
        while (done < data.size() && ::ReadFile(handle, data.data() + done, static_cast<DWORD>((std::min<std::size_t>)(data.size() - done, 1u << 20)), &read, nullptr) && read)
            done += read;
        data.resize(done);
        result = std::move(data);
    }
    CloseHandle(handle);
    return result;
}

bool Private(const std::wstring& lowerName) {
    for (const auto* word : {L"account", L"credential", L"token", L"cookie", L"password", L"secret", L"location", L"session"})
        if (Contains(lowerName, word)) return true;
    return false;
}

// ---- Minimal ZIP (stored, UTF-8 names) ------------------------------------
std::uint32_t Crc32(const std::string& data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            values[i] = c;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
void Put16(std::string& out, std::uint32_t v) { out.push_back(static_cast<char>(v & 0xFF)); out.push_back(static_cast<char>((v >> 8) & 0xFF)); }
void Put32(std::string& out, std::uint32_t v) { Put16(out, v & 0xFFFF); Put16(out, v >> 16); }
std::pair<std::uint16_t, std::uint16_t> DosTime(const FILETIME& value) {
    FILETIME local{};
    SYSTEMTIME t{};
    if (!FileTimeToLocalFileTime(&value, &local) || !FileTimeToSystemTime(&local, &t) || t.wYear < 1980) return {std::uint16_t{0}, std::uint16_t{0x21}};
    return {static_cast<std::uint16_t>((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2)),
            static_cast<std::uint16_t>(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay)};
}
bool WriteZip(const fs::path& path, const std::vector<Entry>& entries) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    std::string central;
    std::uint64_t offset = 0;
    for (const auto& entry : entries) {
        const auto crc = Crc32(entry.bytes);
        const auto [time, date] = DosTime(entry.time);
        const auto size = static_cast<std::uint32_t>(entry.bytes.size());
        std::string header;
        Put32(header, 0x04034b50); Put16(header, 20); Put16(header, 0x0800); Put16(header, 0);
        Put16(header, time); Put16(header, date); Put32(header, crc); Put32(header, size); Put32(header, size);
        Put16(header, static_cast<std::uint32_t>(entry.name.size())); Put16(header, 0);
        header += entry.name;
        file.write(header.data(), static_cast<std::streamsize>(header.size()));
        file.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
        Put32(central, 0x02014b50); Put16(central, 20); Put16(central, 20); Put16(central, 0x0800); Put16(central, 0);
        Put16(central, time); Put16(central, date); Put32(central, crc); Put32(central, size); Put32(central, size);
        Put16(central, static_cast<std::uint32_t>(entry.name.size())); Put16(central, 0); Put16(central, 0);
        Put16(central, 0); Put16(central, 0); Put32(central, 0); Put32(central, static_cast<std::uint32_t>(offset));
        central += entry.name;
        offset += header.size() + entry.bytes.size();
    }
    std::string end;
    Put32(end, 0x06054b50); Put16(end, 0); Put16(end, 0);
    Put16(end, static_cast<std::uint32_t>(entries.size())); Put16(end, static_cast<std::uint32_t>(entries.size()));
    Put32(end, static_cast<std::uint32_t>(central.size())); Put32(end, static_cast<std::uint32_t>(offset)); Put16(end, 0);
    file.write(central.data(), static_cast<std::streamsize>(central.size()));
    file.write(end.data(), static_cast<std::streamsize>(end.size()));
    return static_cast<bool>(file) && offset < 0xFFFFFFFFull;
}

FILETIME NowFileTime() { FILETIME now{}; GetSystemTimeAsFileTime(&now); return now; }
FILETIME WriteTime(const fs::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return data.ftLastWriteTime;
    return NowFileTime();
}

struct Budget {
    std::uintmax_t remaining = MaxReportBytes;
    std::vector<std::string> skipped;  // Left out for size, named in README.
    void Charge(std::uintmax_t bytes) { remaining = bytes >= remaining ? 0 : remaining - bytes; }
};
// newestPart: a log keeps its newest `limit` bytes; anything else is whole or
// left out (a cut dump or settings file is useless).
void AddFile(std::vector<Entry>& entries, Budget& budget, const fs::path& file, const std::string& name,
             std::uintmax_t limit, bool newestPart = false) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error) return;
    if (!newestPart && size > limit) { budget.skipped.push_back(name + " (" + std::to_string(size >> 10) + " KB)"); return; }
    const auto take = (std::min)(size, limit);
    if (take > budget.remaining) { budget.skipped.push_back(name + " (" + std::to_string(take >> 10) + " KB)"); return; }
    if (auto data = ReadShared(file, take)) {
        budget.Charge(data->size());
        entries.push_back({name, std::move(*data), WriteTime(file)});
    }
}

// Module states worth reading first: anything reporting an error or failure.
std::vector<std::string> Problems(const Json& modules) {
    std::vector<std::string> problems;
    for (auto item = modules.begin(); item != modules.end(); ++item) {
        if (!item.value().is_object()) continue;
        std::string line;
        for (const auto* field : {"error", "catalogError", "weatherError", "materialStatus", "loadStatus", "status"}) {
            const auto value = item.value().find(field);
            if (value == item.value().end() || !value->is_string()) continue;
            const auto text = value->get<std::string>();
            const bool bad = (std::string(field).find("rror") != std::string::npos && !text.empty()) ||
                text == "failed" || text == "unavailable" || text == "degraded" || text == "error";
            if (bad) line += std::string(" ") + field + "=" + text;
        }
        if (!line.empty()) problems.push_back(item.key() + ":" + line);
    }
    return problems;
}

std::string Summary(const DiagnosticReportInput& input, const Json& system, const Json& problems, std::size_t dumps,
                    const std::vector<std::string>& crashes, const std::vector<std::string>& skipped,
                    const std::vector<SelfCheck>& checks, bool report) {
    std::string text;
    text += std::string(report ? "Lume 诊断报告" : "Lume 诊断信息") + "\r\n生成时间: " + Now() + "\r\nLume 版本: " + input.version + "\r\n";
    const auto& os = system["windows"];
    // Windows 11 keeps "Windows 10" in ProductName; the build tells them apart.
    auto product = os.value("productName", std::string{});
    if (os.value("windows11", false) && product.rfind("Windows 10", 0) == 0) product.replace(8, 2, "11");
    text += "Windows: " + product + " " + os.value("displayVersion", std::string{}) +
            " (build " + std::to_string(os.value("build", 0)) + "." + (os["ubr"].is_number() ? std::to_string(os["ubr"].get<int>()) : std::string("?")) + ")\r\n";
    std::set<std::string> gpus;  // DXGI can list one adapter several times.
    for (const auto& gpu : system["hardware"]["gpus"]) {
        if (gpu.value("software", false)) continue;
        const auto line = "显卡: " + gpu.value("name", std::string{}) + "  驱动 " + gpu.value("driver", std::string{});
        if (gpus.insert(line).second) text += line + "\r\n";
    }
    for (const auto& display : system["displays"])
        text += "显示器: " + display.value("mode", std::string{}) + "  缩放 " + std::to_string(display.value("scalePercent", 100)) + "%" +
                (display.value("primary", false) ? "  主屏" : "") + "\r\n";
    const auto& appearance = system["appearance"];
    if (appearance["transparencyEffects"] == 0) text += "注意: Windows「透明效果」已关闭，毛玻璃材质会失效。\r\n";
    if (system["hardware"].contains("power") && system["hardware"]["power"].value("batterySaver", false)) text += "注意: 节电模式已开启，Windows 会关闭透明效果。\r\n";
    if (system["windows"].value("remoteSession", false)) text += "注意: 当前为远程桌面会话。\r\n";
    if (!system["shell"]["explorerThirdPartyModules"].empty())
        text += "Explorer 中加载了 " + std::to_string(system["shell"]["explorerThirdPartyModules"].size()) + " 个第三方模块（见 system.json）。\r\n";
    if (!system["relatedSoftware"].empty()) text += "可能冲突的软件: " + system["relatedSoftware"].dump() + "\r\n";
    text += "\r\n启动自检:\r\n";
    for (const auto& check : checks) {
        const char* mark = check.status == CheckStatus::Ok ? "正常" : check.status == CheckStatus::Problem ? "异常" : "未知";
        text += "  [" + std::string(mark) + "] " + check.title + (check.detail.empty() ? "" : "：" + check.detail) + "\r\n";
        if (!check.fix.empty()) text += "         建议：" + check.fix + "\r\n";
    }
    text += "\r\n异常组件:\r\n";
    if (problems.empty()) text += "  （无）\r\n";
    for (const auto& problem : problems) text += "  " + problem.get<std::string>() + "\r\n";
    if (report) text += "\r\n崩溃转储: " + std::to_string(dumps) + " 个\r\n";
    text += "\r\n近 30 天 Windows 记录的崩溃/无响应: " + std::to_string(crashes.size()) + " 条\r\n";
    for (std::size_t i = 0; i < crashes.size() && i < (report ? 8u : 3u); ++i) text += "  " + crashes[i] + "\r\n";
    if (!report) {
        text += "\r\n日志位置: " + input.logDirectory.u8string() + "\r\n";
        if (!input.note.empty()) text += "\r\n用户描述: " + input.note + "\r\n";
        text += "\r\n需要更多信息时，请在 Lume 设置 > 问题反馈 中生成诊断报告并发送 zip 文件。\r\n";
        return text;
    }
    if (!skipped.empty()) {
        text += "\r\n因报告大小上限（" + std::to_string(MaxReportBytes >> 20) + " MB）未包含:\r\n";
        for (const auto& name : skipped) text += "  " + name + "\r\n";
    }
    text += "\r\n用户描述:\r\n" + (input.note.empty() ? std::string("（未填写）") : input.note) + "\r\n";
    text += "\r\n文件说明:\r\n"
            "  system.json      系统、显卡、显示器、外观与任务栏环境\r\n"
            "  processes.json   Lume 进程树（内存、显存、命令行）\r\n"
            "  modules.json     各组件当前状态\r\n"
            "  logs/            Lume 运行日志（错误时间线、退出码、内存心跳）\r\n"
            "  crashes/         Lume 崩溃转储\r\n"
            "  windows-events.txt / wer/  Windows 记录的崩溃与无响应\r\n"
            "  settings/        组件偏好设置（不含账号与位置）\r\n";
    return text;
}

fs::path Desktop() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}
}  // namespace

std::string DiagnosticText(const DiagnosticReportInput& input) {
    try {
        const auto system = DescribeSystem();
        Json problems = Json::array();
        for (const auto& problem : Problems(input.modules)) problems.push_back(problem);
        return Summary(input, system, problems, 0, CrashEvents().rows, {}, RunSelfChecks(input.installDirectory), false);
    } catch (const std::exception& error) {
        return std::string("Lume ") + input.version + " 诊断信息生成失败: " + error.what();
    }
}

std::optional<DiagnosticReportCommand> ParseDiagnosticReportCommand(std::wstring_view message) {
    if (message.empty() || message.size() > 4096 || message.find(L"diagnostic-report") == std::wstring_view::npos) return std::nullopt;
    const auto value = Json::parse(Utf8(std::wstring(message)), nullptr, false);
    if (!value.is_object() || value.value("version", 0) != 1 || value.value("scope", std::string{}) != "diagnostic-report") return std::nullopt;
    DiagnosticReportCommand command{value.value("action", std::string{}), value.value("note", std::string{})};
    if (command.action != "create" && command.action != "open-logs" && command.action != "copy" &&
        command.action != "self-check") return std::nullopt;
    if (command.note.size() > 4000) command.note.resize(4000);
    return command;
}

Json DescribeSystem() {
    Json displays = Json::array();
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&displays));
    return {{"windows", Windows()}, {"hardware", Hardware()}, {"displays", displays}, {"appearance", Appearance()},
            {"shell", ShellEnvironment()}, {"relatedSoftware", RelatedSoftware()}};
}

DiagnosticReportResult WriteDiagnosticReport(const DiagnosticReportInput& input) {
    DiagnosticReportResult result;
    try {
        std::vector<Entry> entries;
        const auto now = NowFileTime();
        auto system = DescribeSystem();
        system["installation"] = Installation(input.installDirectory);
        const auto dump = [](const Json& value) { return value.dump(2, ' ', false, Json::error_handler_t::replace); };
        Json problems = Json::array();
        for (const auto& problem : Problems(input.modules)) problems.push_back(problem);

        Budget budget;
        const auto add = [&](std::string name, std::string bytes) {
            budget.Charge(bytes.size());
            entries.push_back({std::move(name), std::move(bytes), now});
        };
        add("system.json", dump(system));
        add("processes.json", Utf8(resources::EncodeResourceSnapshot(resources::SampleProcessTree(GetCurrentProcessId()))));
        add("modules.json", dump(input.modules));
        const auto crashes = CrashEvents();
        add("windows-events.txt", crashes.text);
        const auto checks = RunSelfChecks(input.installDirectory);
        add("self-check.json", dump(SelfChecksJson(checks)));

        std::error_code error;
        const auto newest = [](std::vector<fs::directory_entry>& list) {
            std::sort(list.begin(), list.end(), [](const auto& left, const auto& right) {
                std::error_code ignored;
                return left.last_write_time(ignored) > right.last_write_time(ignored);
            });
        };
        if (!input.logDirectory.empty())
            for (const auto* name : {L"lume.log", L"lume.1.log"})
                AddFile(entries, budget, input.logDirectory / name, "logs/" + Utf8(name), MaxLogBytes, true);
        if (!input.dataDirectory.empty()) {
            for (const auto& entry : fs::directory_iterator(input.dataDirectory, error)) {
                if (!entry.is_regular_file(error)) continue;
                const auto name = Lower(entry.path().filename().wstring());
                const auto extension = Lower(entry.path().extension().wstring());
                if (Private(name)) continue;
                if (extension == L".json")
                    AddFile(entries, budget, entry.path(), "settings/" + entry.path().filename().u8string(), MaxSettingBytes);
                else if (extension == L".log")
                    AddFile(entries, budget, entry.path(), "logs/" + entry.path().filename().u8string(), MaxLogBytes, true);
            }
        }
        // Windows Error Reporting keeps a text report per crash; the newest
        // ones for Lume processes name the faulting module and offset.
        wchar_t local[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
            std::vector<fs::directory_entry> reports;
            for (const auto* folder : {L"Microsoft\\Windows\\WER\\ReportArchive", L"Microsoft\\Windows\\WER\\ReportQueue"})
                for (const auto& entry : fs::directory_iterator(fs::path(local) / folder, error)) {
                    const auto name = Lower(entry.path().filename().wstring());
                    if (Contains(name, L"lume") || Contains(name, L"mpv.exe") || Contains(name, L"msedgewebview2")) reports.push_back(entry);
                }
            newest(reports);
            for (std::size_t i = 0; i < reports.size() && i < MaxWerReports; ++i)
                AddFile(entries, budget, reports[i].path() / L"Report.wer", "wer/" + reports[i].path().filename().u8string() + ".wer", MaxWerBytes);
        }
        // Dumps last: the largest items, and the newest crash matters most.
        std::size_t dumpCount = 0;
        if (!input.logDirectory.empty()) {
            std::vector<fs::directory_entry> dumps;
            for (const auto& entry : fs::directory_iterator(input.logDirectory / L"crashes", error))
                if (entry.path().extension() == L".dmp") dumps.push_back(entry);
            newest(dumps);
            for (const auto& entry : dumps) {
                if (dumpCount >= MaxDumps) break;
                const auto before = entries.size();
                AddFile(entries, budget, entry.path(), "crashes/" + entry.path().filename().u8string(), MaxDumpBytes);
                if (entries.size() > before) ++dumpCount;
            }
        }
        entries.insert(entries.begin(), Entry{"README.txt", "\xEF\xBB\xBF" + Summary(input, system, problems, dumpCount, crashes.rows, budget.skipped, checks, true), now});

        auto folder = input.outputDirectory.empty() ? Desktop() : input.outputDirectory;
        if (folder.empty()) { result.error = "DIAGNOSTIC_REPORT_NO_DESKTOP"; return result; }
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t name[96]{};
        swprintf_s(name, L"Lume诊断报告-%04u%02u%02u-%02u%02u%02u.zip", time.wYear, time.wMonth, time.wDay, time.wHour,
                   time.wMinute, time.wSecond);
        const auto target = folder / name;
        auto partial = target;
        partial += L".partial";
        if (!WriteZip(partial, entries) || !MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            fs::remove(partial, error);
            result.error = "DIAGNOSTIC_REPORT_WRITE_FAILED";
            return result;
        }
        result.ok = true;
        result.path = target;
        platform::windows::diag::Log("diagnostics", "report written: " + target.filename().u8string() + ", " +
                                     std::to_string(entries.size()) + " files");
    } catch (const std::exception& error) {
        result.error = std::string("DIAGNOSTIC_REPORT_FAILED: ") + error.what();
    }
    return result;
}
}  // namespace lume::shell
