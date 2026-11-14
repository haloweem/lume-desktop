#include "apps/shell/self_check.h"
#include "platform/windows/text_util.h"
#include "platform/windows/taskbar/taskbar_frameworks.h"

#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace lume::shell {
namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using lume::text::Utf8;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
DWORD Build() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (const auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) get(&version);
    return version.dwBuildNumber;
}
std::string Sha256(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) != 0) return {};
    std::array<char, 1 << 16> buffer{};
    while (in) {
        in.read(buffer.data(), buffer.size());
        if (in.gcount() > 0) BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(in.gcount()), 0);
    }
    std::array<unsigned char, 32> digest{};
    const bool ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    BCryptDestroyHash(hash);
    if (!ok) return {};
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string text;
    for (const auto byte : digest) { text.push_back(hex[byte >> 4]); text.push_back(hex[byte & 15]); }
    return text;
}
const char* Arch() {
    USHORT process = 0, native = 0;
    using Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    if (const auto get = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")))
        if (get(GetCurrentProcess(), &process, &native) && native == IMAGE_FILE_MACHINE_ARM64) return "arm64";
    return "x64";
}

// "msvcp140.dll was not found": Lume itself links the runtime statically; the
// taskbar core uses the copies shipped beside it, and mpv the Windows UCRT.
SelfCheck Runtime(const fs::path& install) {
    SelfCheck check{"runtime", CheckStatus::Unknown, "运行库（msvcp140 等）", {}, {}};
    std::vector<std::string> problems;
    const auto ucrt = LoadLibraryExW(L"ucrtbase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ucrt) FreeLibrary(ucrt);
    else problems.push_back("系统缺少 Universal C Runtime（ucrtbase.dll）");
    const auto folder = install / L"taskbar-runtime";
    std::map<std::string, std::string> expected;
    std::ifstream sums(folder / L"SHA256SUMS.txt");
    for (std::string line; std::getline(sums, line);) {
        const auto star = line.find(" *");
        if (star == 64) expected[line.substr(star + 2)] = line.substr(0, 64);
    }
    const std::string arch = Arch();
    std::size_t checked = 0;
    for (const auto* name : {"msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll"}) {
        const auto relative = arch + "/" + name;
        const auto file = folder / fs::u8path(relative);
        std::error_code error;
        if (!fs::is_regular_file(file, error)) {
            if (arch == "arm64" && std::string(name) == "vcruntime140_1.dll") continue;  // x64 only
            problems.push_back("缺少 taskbar-runtime\\" + arch + "\\" + name);
            continue;
        }
        const auto want = expected.find(relative);
        if (want == expected.end()) continue;
        const auto got = Sha256(file);
        if (got.empty()) continue;
        ++checked;
        if (got != want->second) problems.push_back("taskbar-runtime\\" + arch + "\\" + name + " 已被改动");
    }
    if (!problems.empty()) {
        check.status = CheckStatus::Problem;
        for (const auto& item : problems) check.detail += (check.detail.empty() ? "" : "；") + item;
        check.fix = "多半是安全软件删除或替换了这些文件：把 Lume 安装目录加入安全软件的信任区后重新安装 Lume。"
                    "若提示缺少 ucrtbase.dll，请运行 Windows 更新。";
    } else if (checked) {
        check.status = CheckStatus::Ok;
        check.detail = "运行库文件齐全且与安装包一致";
    } else {
        check.detail = "未能读取安装目录中的校验清单";
    }
    return check;
}

SelfCheck Wallpaper(const fs::path& install) {
    SelfCheck check{"wallpaper", CheckStatus::Ok, "动态壁纸播放器", "mpv 与解密插件齐全", {}};
    std::vector<std::string> missing;
    for (const auto* name : {L"wallpaper-runtime\\mpv.exe", L"wallpaper-runtime\\lume-mpv-stream.dll", L"wallpaper-runtime\\d3dcompiler_43.dll"}) {
        std::error_code error;
        if (!fs::is_regular_file(install / name, error)) missing.push_back(Utf8(name));
    }
    if (!missing.empty()) {
        check.status = CheckStatus::Problem;
        check.detail = "缺少";
        for (const auto& item : missing) check.detail += " " + item;
        check.fix = "安全软件可能拦截或删除了壁纸播放器：把 Lume 安装目录加入信任区后重新安装 Lume。";
    }
    return check;
}

// Helper programs security software tends to quarantine: without
// LumeIconHost.exe the organizer shows no icons (items still open in process).
SelfCheck Helpers(const fs::path& install) {
    SelfCheck check{"helpers", CheckStatus::Ok, "辅助程序", "图标与音乐辅助程序齐全", {}};
    std::string missing;
    for (const auto* name : {L"LumeIconHost.exe", L"LumeMusicHost.exe"}) {
        std::error_code error;
        if (!fs::is_regular_file(install / name, error)) missing += (missing.empty() ? "" : "、") + Utf8(name);
    }
    if (!missing.empty()) {
        check.status = CheckStatus::Problem;
        check.detail = "缺少 " + missing + "（图标收纳的图标、本地音乐可能无法使用）";
        check.fix = "安全软件可能删除了这些文件：把 Lume 安装目录加入信任区后重新安装 Lume。";
    }
    return check;
}

// Whether the GPU decodes the codecs official wallpapers use. Without it mpv
// falls back to software decoding: it still plays, at a much higher CPU cost.
SelfCheck GpuDecode() {
    SelfCheck check{"gpu-decode", CheckStatus::Unknown, "壁纸显卡解码", {}, {}};
    ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, &level, nullptr))) {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                     &device, &level, nullptr))) {
            check.status = CheckStatus::Problem;
            check.detail = "无法创建 Direct3D 11 硬件设备";
            check.fix = "请安装或更新显卡驱动（不要使用 Windows 自带的“基本显示适配器”驱动）。";
            return check;
        }
        check.status = CheckStatus::Problem;
        check.detail = "显卡驱动不提供视频解码";
        check.fix = "动态壁纸会改用 CPU 解码，耗电和占用更高：请更新显卡驱动，或在设置中开启平衡模式。";
        return check;
    }
    ComPtr<ID3D11VideoDevice> video;
    if (FAILED(device.As(&video))) { check.detail = "无法查询视频解码能力"; return check; }
    static const GUID H264 = {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
    static const GUID Hevc = {0x5b11d51b, 0x2f4c, 0x4452, {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}};
    bool h264 = false, hevc = false;
    for (UINT i = 0, count = video->GetVideoDecoderProfileCount(); i < count; ++i) {
        GUID profile{};
        if (FAILED(video->GetVideoDecoderProfile(i, &profile))) continue;
        h264 = h264 || IsEqualGUID(profile, H264);
        hevc = hevc || IsEqualGUID(profile, Hevc);
    }
    if (h264) {
        check.status = CheckStatus::Ok;
        check.detail = std::string("支持 H.264") + (hevc ? " 与 HEVC" : "（不支持 HEVC，HEVC 壁纸将用 CPU 解码）") + " 硬件解码";
    } else {
        check.status = CheckStatus::Problem;
        check.detail = "显卡不支持 H.264 硬件解码";
        check.fix = "动态壁纸会改用 CPU 解码，耗电和占用更高：请更新显卡驱动，或在设置中开启平衡模式。";
    }
    return check;
}

// What keeps Lume's transparent taskbar from working: another tool already
// styling the taskbar, a core from another Lume copy, a missing core file.
bool OwnCore(const PROCESSENTRY32W& entry, HANDLE process) {
    if (entry.th32ParentProcessID != GetCurrentProcessId()) return false;
    FILETIME ownerCreated{}, childCreated{}, exited{}, kernel{}, user{};
    // The core is staged into the data directory before launch. Its path is
    // shared by installations and cannot establish process ownership. Check
    // creation times as well so a recycled parent PID cannot look like ours.
    return GetProcessTimes(GetCurrentProcess(), &ownerCreated, &exited, &kernel, &user) &&
        GetProcessTimes(process, &childCreated, &exited, &kernel, &user) &&
        CompareFileTime(&childCreated, &ownerCreated) >= 0;
}
SelfCheck Taskbar(const fs::path& install, bool transparency = true) {
    SelfCheck check{"taskbar", CheckStatus::Ok, "透明任务栏", "运行环境检查通过，尚未验证透明效果", {}};
    const auto core = install / L"taskbar-runtime" / fs::u8path(Arch()) / L"LumeTaskbarCore.exe";
    std::error_code error;
    std::vector<std::string> problems;
    std::string unknownFramework;
    bool missingFramework = false;
    const bool windows11 = Build() >= 22000;
    const bool missingCore = transparency && windows11 && !fs::is_regular_file(core, error);
    if (missingCore) problems.push_back("缺少 LumeTaskbarCore.exe");
    if (transparency && windows11) {
        using namespace platform::windows;
        const auto frameworks = QueryTaskbarFrameworks(std::string(Arch()) == "arm64");
        if (frameworks.hresult < 0) {
            std::ostringstream code; code << "0x" << std::hex << static_cast<std::uint32_t>(frameworks.hresult);
            unknownFramework = "无法检查当前用户的任务栏框架（" + code.str() + "）";
        } else {
            if (frameworks.vclibsVersion < TaskbarVCLibsMinimum)
                problems.push_back("缺少可用的 Microsoft.VCLibs.140.00（14.0.33519.0 或更高版本，" + std::string(Arch()) + "）");
            if (frameworks.winuiVersion < TaskbarWinUIMinimum)
                problems.push_back("缺少满足当前版本要求的 Microsoft.UI.Xaml.2.8（" + std::string(Arch()) + "）");
            missingFramework = !frameworks.Ready();
        }
    }
    // Only tools that certainly restyle the taskbar; a guess would send users
    // after the wrong program.
    static constexpr std::array<const wchar_t*, 3> rivals{L"translucenttb.exe", L"roundedtb.exe", L"taskbarx.exe"};
    std::set<std::string> found;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    bool listed = false;
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            listed = true;
            const auto name = Lower(entry.szExeFile);
            for (const auto rival : rivals) if (transparency && name.find(rival) != std::wstring::npos) found.insert(Utf8(entry.szExeFile));
            if (transparency && name == L"lumetaskbarcore.exe") {
                // Another instance can use exactly the same cache or install
                // path. Only a verified child of this process is ours.
                wchar_t path[32768]{};
                DWORD size = 32768;
                const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process) {
                    if (!OwnCore(entry, process) && QueryFullProcessImageNameW(process, 0, path, &size))
                        found.insert("另一份 Lume 的任务栏核心（" + Utf8(path) + "）");
                    CloseHandle(process);
                }
            }
        }
        CloseHandle(snapshot);
    }
    for (const auto& item : found) problems.push_back("正在运行 " + item);
    // StartAllBack / ExplorerPatcher replace the taskbar without a process of
    // their own: their DLLs show in the diagnostic report's Explorer modules.
    if (!problems.empty() || !unknownFramework.empty()) {
        check.status = problems.empty() ? CheckStatus::Unknown : CheckStatus::Problem;
        check.detail.clear();
        for (const auto& item : problems) check.detail += (check.detail.empty() ? "" : "；") + item;
        if (!unknownFramework.empty()) check.detail += (check.detail.empty() ? "" : "；") + unknownFramework;
        if (missingFramework || !unknownFramework.empty())
            check.fix += "请重新启用透明任务栏，Lume 会自动补齐所需框架；若仍失败，请导出诊断报告。";
        if (!found.empty())
            check.fix += "同时只能有一个程序修改任务栏：退出上面列出的程序（或其开机启动）后，重新打开透明任务栏。";
        if (missingCore)
            check.fix += "请把 Lume 安装目录加入安全软件信任区后重新安装，以恢复缺少的核心文件。";
    } else if (!listed) {
        check.status = CheckStatus::Unknown;
        check.detail = "无法列出正在运行的程序";
    }
    return check;
}
}  // namespace
void ApplyTaskbarRuntimeState(SelfCheck& check, const nlohmann::json& modules) {
    const auto unknown = [&] {
        if (check.status == CheckStatus::Ok) {
            check.status = CheckStatus::Unknown;
            check.detail = "运行环境检查通过，尚未收到任务栏效果确认";
        }
    };
    if (!modules.is_object()) { unknown(); return; }
    const auto found = modules.find("taskbar-state");
    if (found == modules.end() || !found->is_object()) { unknown(); return; }
    const auto& state = *found;
    const auto flag = [&](const char* name) {
        const auto item = state.find(name);
        return item != state.end() && item->is_boolean() && item->get<bool>();
    };
    const auto errorValue = state.find("error");
    const auto error = errorValue != state.end() && errorValue->is_string()
        ? errorValue->get<std::string>() : std::string{};
    const auto effectValue = state.find("effectStatus");
    const auto effect = effectValue != state.end() && effectValue->is_string()
        ? effectValue->get<std::string>() : std::string{};
    const auto stageValue = state.find("stage");
    const auto stage = stageValue != state.end() && stageValue->is_string()
        ? stageValue->get<std::string>().substr(0, 96) : std::string{};
    const auto hrValue = state.find("hresult");
    const auto hr = hrValue != state.end() && hrValue->is_number_integer()
        ? hrValue->get<std::int64_t>() : 0;
    std::string evidence;
    if (!stage.empty()) evidence += "阶段=" + stage;
    if (hr != 0) {
        std::ostringstream code;
        code << "0x" << std::hex << static_cast<std::uint32_t>(hr);
        evidence += (evidence.empty() ? "" : "，") + code.str();
    }
    const bool hiding = (flag("enabled") && flag("autoHideTaskbar")) || flag("autoHideDemand");
    const bool hideApplied = hiding && flag("hideApplied") && flag("effectiveAutoHide");
    const auto hideDetail = flag("fullyHidden") ? "任务栏完全隐藏已应用" :
        flag("autoHideDemand") && !flag("autoHideTaskbar") ? "Dock 请求的任务栏自动隐藏已应用" : "任务栏自动隐藏已应用";
    if (!error.empty() || (flag("pendingRecovery") && !flag("applying"))) {
        const auto detail = error == "core-access-denied"
            ? "任务栏透明服务接入系统任务栏时被拒绝访问（0x80070005），已停止自动重试"
            : error == "core-restart-required" ? "任务栏接入模块仍被 Explorer 占用或版本不一致，需要重新加载，已停止自动重试"
            : error == "core-attach-timeout" ? "任务栏接入系统 XAML 超时（0x80070102），尚未确认透明效果"
            : error == "core-effect-unconfirmed" ? "任务栏服务正在运行，但尚未确认背景透明效果"
            : error.empty() ? std::string("原任务栏设置仍待恢复") : "任务栏运行失败（" + error + "）";
        check.detail = check.status == CheckStatus::Ok ? detail : check.detail + "；" + detail;
        if (hideApplied) check.detail += std::string("；") + hideDetail;
        check.status = CheckStatus::Problem;
        check.fix += error == "core-restart-required"
            ? "请保存工作后重启 Windows，再启动当前安装的 Lume；不需要反复切换透明开关。"
            : "请导出问题反馈报告，以核对任务栏背景、运行错误和权限信息。";
    } else if (check.status == CheckStatus::Ok) {
        if (flag("applying") || (flag("enabled") && !flag("running"))) {
            check.status = CheckStatus::Unknown;
            check.detail = "任务栏服务尚未完成初始化，尚未确认效果";
        } else if (flag("enabled") && flag("transparentTaskbar") && effect != "applied") {
            check.status = effect == "failed" ? CheckStatus::Problem : CheckStatus::Unknown;
            check.detail = effect == "failed" ? "任务栏背景透明效果应用失败" : "任务栏服务正在运行，但尚未确认背景透明效果";
            if (hideApplied) check.detail += std::string("；") + hideDetail;
            check.fix = "请导出问题反馈报告，以核对实际背景匹配和画刷应用状态。";
        } else if (hiding && !hideApplied) {
            check.status = CheckStatus::Unknown;
            check.detail = "尚未确认任务栏隐藏效果";
        } else if (flag("enabled") && flag("transparentTaskbar")) {
            check.detail = "任务栏背景透明效果已确认";
            if (hideApplied) check.detail += std::string("；") + hideDetail;
        } else if (hideApplied) check.detail = hideDetail;
        else check.detail = "任务栏效果已关闭，运行环境检查通过";
    }
    if (!evidence.empty() && (check.status != CheckStatus::Ok || hr != 0)) check.detail += "（" + evidence + "）";
}

std::vector<SelfCheck> RunSelfChecks(const fs::path& installDirectory, const nlohmann::json& modules) {
    std::vector<SelfCheck> checks;
    const auto guard = [&](SelfCheck (*run)(const fs::path&), const char* id, const char* title) {
        try { checks.push_back(run(installDirectory)); }
        catch (...) { checks.push_back({id, CheckStatus::Unknown, title, "检查时出错", {}}); }
    };
    guard(Runtime, "runtime", "运行库（msvcp140 等）");
    guard(Wallpaper, "wallpaper", "动态壁纸播放器");
    guard(Helpers, "helpers", "辅助程序");
    try { checks.push_back(GpuDecode()); } catch (...) { checks.push_back({"gpu-decode", CheckStatus::Unknown, "壁纸显卡解码", "检查时出错", {}}); }
    bool transparency = true;
    if (modules.is_object()) {
        const auto state = modules.find("taskbar-state");
        if (state != modules.end() && state->is_object()) {
            const auto enabled = state->find("transparentTaskbar");
            if (enabled != state->end() && enabled->is_boolean()) transparency = enabled->get<bool>();
        }
    }
    try { checks.push_back(Taskbar(installDirectory, transparency)); }
    catch (...) { checks.push_back({"taskbar", CheckStatus::Unknown, "任务栏美化", "检查时出错", {}}); }
    ApplyTaskbarRuntimeState(checks.back(), modules);
    return checks;
}

const char* CheckStatusName(CheckStatus status) {
    return status == CheckStatus::Ok ? "ok" : status == CheckStatus::Problem ? "problem" : "unknown";
}

nlohmann::json SelfChecksJson(const std::vector<SelfCheck>& checks) {
    auto list = nlohmann::json::array();
    for (const auto& check : checks)
        list.push_back({{"id", check.id}, {"status", CheckStatusName(check.status)}, {"title", check.title},
                        {"detail", check.detail}, {"fix", check.fix}});
    return list;
}

std::string SelfCheckSignature(const std::vector<SelfCheck>& checks) {
    std::string text;
    for (const auto& check : checks)
        if (check.status == CheckStatus::Problem) text += check.id + ":" + check.detail + "\n";
    return text;
}
}  // namespace lume::shell
