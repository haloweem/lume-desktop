#include <winsock2.h>
#include <ws2ipdef.h>
#include <iptypes.h>
#include "platform/windows/control_center/system_controls.h"
#include "platform/windows/silent_privacy.h"
#include <wlanapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <uiautomation.h>
#include <wbemidl.h>
#include <iphlpapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.NetworkOperators.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

namespace lume::platform::windows::control_center {
namespace {
using winrt::com_ptr;
namespace Radios = winrt::Windows::Devices::Radios;
namespace Operators = winrt::Windows::Networking::NetworkOperators;
namespace Connectivity = winrt::Windows::Networking::Connectivity;

// --- radios ---------------------------------------------------------------
std::vector<Radios::Radio> AllRadios() {
    std::vector<Radios::Radio> out;
    try { for (auto radio : Radios::Radio::GetRadiosAsync().get()) out.push_back(radio); } catch (...) {}
    return out;
}
bool Wireless(Radios::RadioKind kind) {
    return kind == Radios::RadioKind::WiFi || kind == Radios::RadioKind::Bluetooth || kind == Radios::RadioKind::MobileBroadband;
}
bool SetState(Radios::Radio& radio, bool on) {
    try {
        return radio.SetStateAsync(on ? Radios::RadioState::On : Radios::RadioState::Off).get() == Radios::RadioAccessStatus::Allowed;
    } catch (...) { return false; }
}
// Radios turned off by Lume's airplane mode, restored when it is switched off.
std::set<std::wstring> airplaneRestore;

// --- Wi-Fi network ----------------------------------------------------------
void ReadWifi(State& state) {
    if (!HasExistingCapabilityAccess(L"wiFiControl")) return;
    HANDLE handle = nullptr;
    DWORD version = 0;
    if (WlanOpenHandle(2, nullptr, &version, &handle) != ERROR_SUCCESS) return;
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(handle, nullptr, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems && state.ssid.empty(); ++i) {
            if (list->InterfaceInfo[i].isState != wlan_interface_state_connected) continue;
            DWORD size = 0;
            PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
            if (WlanQueryInterface(handle, &list->InterfaceInfo[i].InterfaceGuid, wlan_intf_opcode_current_connection, nullptr,
                                   &size, reinterpret_cast<void**>(&attributes), nullptr) != ERROR_SUCCESS || !attributes) continue;
            const auto& ssid = attributes->wlanAssociationAttributes.dot11Ssid;
            const std::string bytes(reinterpret_cast<const char*>(ssid.ucSSID), std::min<ULONG>(ssid.uSSIDLength, 32));
            const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            std::wstring name(static_cast<std::size_t>(std::max(length, 0)), L'\0');
            if (length > 0) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), name.data(), length);
            state.ssid = name;
            state.signal = static_cast<int>(attributes->wlanAssociationAttributes.wlanSignalQuality);
            WlanFreeMemory(attributes);
        }
        WlanFreeMemory(list);
    }
    WlanCloseHandle(handle, nullptr);
}
bool EthernetUp() {
    ULONG bytes = 16 * 1024;
    std::vector<unsigned char> buffer(bytes);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                                 nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bytes);
        if (result == ERROR_BUFFER_OVERFLOW) { buffer.resize(bytes); continue; }
        if (result != NO_ERROR) return false;
        for (auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter; adapter = adapter->Next)
            if (adapter->OperStatus == IfOperStatusUp && adapter->IfType == IF_TYPE_ETHERNET_CSMACD && adapter->FirstUnicastAddress)
                return true;
        return false;
    }
    return false;
}

// --- mobile hotspot -------------------------------------------------------
std::optional<Operators::NetworkOperatorTetheringManager> Tethering(std::wstring& reason) {
    try {
        const auto profile = Connectivity::NetworkInformation::GetInternetConnectionProfile();
        if (!profile) { reason = L"no-network"; return std::nullopt; }
        const auto capability = Operators::NetworkOperatorTetheringManager::GetTetheringCapabilityFromConnectionProfile(profile);
        if (capability != Operators::TetheringCapability::Enabled) {
            reason = capability == Operators::TetheringCapability::DisabledByGroupPolicy ? L"policy" :
                     capability == Operators::TetheringCapability::DisabledByHardwareLimitation ? L"hardware" : L"unsupported";
            return std::nullopt;
        }
        return Operators::NetworkOperatorTetheringManager::CreateFromConnectionProfile(profile);
    } catch (...) { reason = L"unsupported"; return std::nullopt; }
}

// --- audio ------------------------------------------------------------------
// Undocumented but stable since Windows 7; the only way to change the
// default output device. Used only for SetDefaultEndpoint.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
constexpr CLSID PolicyConfigClient{0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
com_ptr<IMMDeviceEnumerator> Devices() {
    com_ptr<IMMDeviceEnumerator> devices;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(devices.put()));
    return devices;
}
com_ptr<IAudioEndpointVolume> Endpoint() {
    com_ptr<IAudioEndpointVolume> endpoint;
    com_ptr<IMMDevice> device;
    if (const auto devices = Devices(); devices && SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put())))
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, endpoint.put_void());
    return endpoint;
}
std::wstring DeviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    std::wstring out;
    if (SUCCEEDED(device->GetId(&id)) && id) out = id;
    CoTaskMemFree(id);
    return out;
}
void ReadAudio(State& state) {
    const auto devices = Devices();
    if (!devices) return;
    com_ptr<IMMDevice> current;
    if (SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, current.put()))) state.output = DeviceId(current.get());
    com_ptr<IMMDeviceCollection> all;
    UINT count = 0;
    if (SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, all.put())) && SUCCEEDED(all->GetCount(&count)))
        for (UINT i = 0; i < count && i < 32; ++i) {
            com_ptr<IMMDevice> device;
            com_ptr<IPropertyStore> properties;
            if (FAILED(all->Item(i, device.put())) || FAILED(device->OpenPropertyStore(STGM_READ, properties.put()))) continue;
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal)
                state.outputs.push_back({DeviceId(device.get()), name.pwszVal});
            PropVariantClear(&name);
        }
    if (const auto endpoint = Endpoint()) {
        float level = 0;
        BOOL muted = FALSE;
        if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&level)) && SUCCEEDED(endpoint->GetMute(&muted))) {
            state.audioAvailable = true;
            state.volume = static_cast<int>(std::lround(level * 100));
            state.muted = muted != FALSE;
        }
    }
}

// --- brightness -------------------------------------------------------------
struct DisplayInfo {
    std::wstring device, name;
    bool internal = false;
};
std::vector<DisplayInfo> ActiveDisplays() {
    std::vector<DisplayInfo> out;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return out;
    paths.resize(pathCount);
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id};
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        DisplayInfo info;
        info.device = source.viewGdiDeviceName;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) info.name = target.monitorFriendlyDeviceName;
        const auto tech = path.targetInfo.outputTechnology;
        info.internal = tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                        tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        if (info.name.empty()) info.name = info.internal ? L"内置显示器" : info.device.substr(info.device.find_last_of(L'\\') + 1);
        if (std::none_of(out.begin(), out.end(), [&](const auto& d) { return d.device == info.device; })) out.push_back(info);
    }
    return out;
}
HMONITOR MonitorFor(const std::wstring& device) {
    struct Search { const std::wstring* device; HMONITOR found = nullptr; } search{&device};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) && *s.device == info.szDevice) { s.found = monitor; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
// DDC/CI over the monitor cable: external displays.
template <typename F> bool WithPhysical(const std::wstring& device, F&& use) {
    const auto monitor = MonitorFor(device);
    DWORD count = 0;
    if (!monitor || !GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || !count || count > 8) return false;
    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) return false;
    const bool ok = use(physical[0].hPhysicalMonitor);
    DestroyPhysicalMonitors(count, physical.data());
    return ok;
}
std::optional<int> DdcRead(const std::wstring& device) {
    int percent = -1;
    WithPhysical(device, [&](HANDLE handle) {
        DWORD minimum = 0, current = 0, maximum = 0;
        if (!GetMonitorBrightness(handle, &minimum, &current, &maximum) || maximum <= minimum) return false;
        percent = static_cast<int>(std::lround(100.0 * (current - minimum) / (maximum - minimum)));
        return true;
    });
    return percent >= 0 ? std::optional<int>(percent) : std::nullopt;
}
bool DdcWrite(const std::wstring& device, int percent) {
    return WithPhysical(device, [&](HANDLE handle) {
        DWORD minimum = 0, current = 0, maximum = 0;
        if (!GetMonitorBrightness(handle, &minimum, &current, &maximum) || maximum <= minimum) return false;
        return SetMonitorBrightness(handle, minimum + static_cast<DWORD>(std::lround((maximum - minimum) * percent / 100.0))) != FALSE;
    });
}
// WMI: laptop and tablet panels.
com_ptr<IWbemServices> Wmi() {
    com_ptr<IWbemLocator> locator;
    com_ptr<IWbemServices> services;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.put())))) return nullptr;
    BSTR space = SysAllocString(L"ROOT\\WMI");
    const HRESULT connected = locator->ConnectServer(space, nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.put());
    SysFreeString(space);
    if (FAILED(connected)) return nullptr;
    CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return services;
}
std::optional<int> WmiRead() {
    const auto services = Wmi();
    if (!services) return std::nullopt;
    com_ptr<IEnumWbemClassObject> rows;
    BSTR language = SysAllocString(L"WQL"), query = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");
    const HRESULT hr = services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, rows.put());
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(hr) || !rows) return std::nullopt;
    com_ptr<IWbemClassObject> row;
    ULONG returned = 0;
    if (FAILED(rows->Next(2000, 1, row.put(), &returned)) || !returned) return std::nullopt;
    VARIANT value;
    VariantInit(&value);
    std::optional<int> out;
    if (SUCCEEDED(row->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr)) && (value.vt == VT_UI1 || value.vt == VT_I4))
        out = value.vt == VT_UI1 ? value.bVal : value.lVal;
    VariantClear(&value);
    return out;
}
bool WmiWrite(int percent) {
    const auto services = Wmi();
    if (!services) return false;
    com_ptr<IEnumWbemClassObject> rows;
    BSTR language = SysAllocString(L"WQL"), query = SysAllocString(L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE");
    const HRESULT hr = services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, rows.put());
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(hr) || !rows) return false;
    com_ptr<IWbemClassObject> row, type, signature, input;
    ULONG returned = 0;
    if (FAILED(rows->Next(2000, 1, row.put(), &returned)) || !returned) return false;
    VARIANT path;
    VariantInit(&path);
    if (FAILED(row->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR) return false;
    BSTR className = SysAllocString(L"WmiMonitorBrightnessMethods"), method = SysAllocString(L"WmiSetBrightness");
    bool ok = SUCCEEDED(services->GetObject(className, 0, nullptr, type.put(), nullptr)) &&
              SUCCEEDED(type->GetMethod(method, 0, signature.put(), nullptr)) && SUCCEEDED(signature->SpawnInstance(0, input.put()));
    if (ok) {
        VARIANT timeout, level;
        VariantInit(&timeout);
        VariantInit(&level);
        timeout.vt = VT_I4;timeout.lVal = 0;
        level.vt = VT_UI1;level.bVal = static_cast<BYTE>(std::clamp(percent, 0, 100));
        ok = SUCCEEDED(input->Put(L"Timeout", 0, &timeout, 0)) && SUCCEEDED(input->Put(L"Brightness", 0, &level, 0)) &&
             SUCCEEDED(services->ExecMethod(path.bstrVal, method, 0, nullptr, input.get(), nullptr, nullptr));
    }
    SysFreeString(className);
    SysFreeString(method);
    VariantClear(&path);
    return ok;
}
// How each adjustable display is driven; refreshed with readBrightness.
struct Adjustable { std::wstring device, name; bool wmi = false; int level = 0; };
std::vector<Adjustable> adjustable;
void ReadBrightness() {
    adjustable.clear();
    bool wmiUsed = false;
    for (const auto& display : ActiveDisplays()) {
        if (display.internal && !wmiUsed) {
            if (const auto level = WmiRead()) {
                adjustable.push_back({display.device, display.name, true, *level});
                wmiUsed = true;
                continue;
            }
        }
        if (const auto level = DdcRead(display.device)) adjustable.push_back({display.device, display.name, false, *level});
    }
}

// --- notification area -----------------------------------------------------
HWND Win10Chevron() {
    const HWND notify = FindWindowExW(FindWindowW(L"Shell_TrayWnd", nullptr), nullptr, L"TrayNotifyWnd", nullptr);
    const HWND button = notify ? FindWindowExW(notify, nullptr, L"Button", nullptr) : nullptr;
    return button && IsWindowVisible(button) ? button : nullptr;
}
// Windows 11: the chevron is the leftmost SystemTray.NormalButton and, unlike
// app icons, has no Image child. Language independent.
com_ptr<IUIAutomationElement> Win11Chevron(com_ptr<IUIAutomation>& automation) {
    if (!FindWindowW(L"TopLevelWindowForOverflowXamlIsland", nullptr)) return nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(automation.put())))) return nullptr;
    com_ptr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(FindWindowW(L"Shell_TrayWnd", nullptr), root.put())) || !root) return nullptr;
    VARIANT name;
    VariantInit(&name);
    name.vt = VT_BSTR;name.bstrVal = SysAllocString(L"SystemTray.NormalButton");
    com_ptr<IUIAutomationCondition> byClass, image;
    automation->CreatePropertyCondition(UIA_ClassNamePropertyId, name, byClass.put());
    VariantClear(&name);
    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4;type.lVal = UIA_ImageControlTypeId;
    automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, image.put());
    com_ptr<IUIAutomationElementArray> buttons;
    if (!byClass || FAILED(root->FindAll(TreeScope_Descendants, byClass.get(), buttons.put())) || !buttons) return nullptr;
    int count = 0;
    buttons->get_Length(&count);
    com_ptr<IUIAutomationElement> best;
    LONG left = LONG_MAX;
    for (int i = 0; i < count; ++i) {
        com_ptr<IUIAutomationElement> button;
        RECT bounds{};
        if (FAILED(buttons->GetElement(i, button.put())) || FAILED(button->get_CurrentBoundingRectangle(&bounds))) continue;
        if (bounds.left < left) { left = bounds.left;best = button; }
    }
    if (!best) return nullptr;
    com_ptr<IUIAutomationElement> child;
    if (image && SUCCEEDED(best->FindFirst(TreeScope_Descendants, image.get(), child.put())) && child) return nullptr;
    return best;
}
}  // namespace

State Read(const std::wstring& display, bool readBrightness) {
    State state;
    for (auto& radio : AllRadios()) {
        const bool on = radio.State() == Radios::RadioState::On;
        if (radio.Kind() == Radios::RadioKind::WiFi && !state.wifi.available) state.wifi = {true, on};
        if (radio.Kind() == Radios::RadioKind::Bluetooth && !state.bluetooth.available) state.bluetooth = {true, on};
        if (Wireless(radio.Kind())) {
            state.airplaneAvailable = true;
            if (on) state.airplaneOn = false;
        }
    }
    // Airplane is on only when every wireless radio is off.
    if (state.airplaneAvailable) {
        state.airplaneOn = true;
        for (auto& radio : AllRadios())
            if (Wireless(radio.Kind()) && radio.State() == Radios::RadioState::On) state.airplaneOn = false;
    }
    if (state.wifi.on) ReadWifi(state);
    state.ethernet = EthernetUp();
    if (const auto manager = Tethering(state.hotspotReason)) {
        state.hotspotAvailable = true;
        try { state.hotspotOn = manager->TetheringOperationalState() == Operators::TetheringOperationalState::On; } catch (...) {}
    }
    ReadAudio(state);
    if (readBrightness) ReadBrightness();
    for (const auto& item : adjustable) state.displays.push_back({item.device, item.name});
    if (!adjustable.empty()) {
        const auto found = std::find_if(adjustable.begin(), adjustable.end(), [&](const auto& a) { return a.device == display; });
        const auto& chosen = found != adjustable.end() ? *found : adjustable.front();
        state.brightnessAvailable = true;
        state.display = chosen.device;
        state.brightness = chosen.level;
    }
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.BatteryLifePercent != 255 && !(power.BatteryFlag & 128)) {
        state.batteryAvailable = true;
        state.battery = power.BatteryLifePercent;
        state.charging = (power.BatteryFlag & BATTERY_FLAG_CHARGING) || power.ACLineStatus == 1;
    }
    // Checked on every read and cheaply: the taskbar exists. Finding the
    // overflow button (UI Automation, slow, and not ready while Explorer
    // starts) waits for the click, so the row never appears late.
    state.trayAvailable = FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    return state;
}

bool SetRadio(bool wifi, bool on) {
    bool ok = false;
    for (auto& radio : AllRadios())
        if (radio.Kind() == (wifi ? Radios::RadioKind::WiFi : Radios::RadioKind::Bluetooth)) ok = SetState(radio, on) || ok;
    if (on) airplaneRestore.clear();
    return ok;
}
bool SetAirplane(bool on) {
    bool ok = false;
    auto radios = AllRadios();
    if (on) {
        airplaneRestore.clear();
        for (auto& radio : radios) {
            if (!Wireless(radio.Kind())) continue;
            if (radio.State() == Radios::RadioState::On) airplaneRestore.insert(std::wstring(radio.Name()));
            ok = SetState(radio, false) || ok;
        }
    } else {
        // Restore what airplane mode switched off, or everything when unknown.
        for (auto& radio : radios)
            if (Wireless(radio.Kind()) && (airplaneRestore.empty() || airplaneRestore.count(std::wstring(radio.Name()))))
                ok = SetState(radio, true) || ok;
        airplaneRestore.clear();
    }
    return ok;
}
bool SetHotspot(bool on) {
    std::wstring reason;
    const auto manager = Tethering(reason);
    if (!manager) return false;
    try {
        const auto result = on ? manager->StartTetheringAsync().get() : manager->StopTetheringAsync().get();
        return result.Status() == Operators::TetheringOperationStatus::Success;
    } catch (...) { return false; }
}
bool SetVolume(int percent) {
    const auto endpoint = Endpoint();
    return endpoint && SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(std::clamp(percent, 0, 100) / 100.f, nullptr)) &&
           (percent == 0 || SUCCEEDED(endpoint->SetMute(FALSE, nullptr)));
}
bool SetMute(bool muted) {
    const auto endpoint = Endpoint();
    return endpoint && SUCCEEDED(endpoint->SetMute(muted, nullptr));
}
bool SetOutput(const std::wstring& id) {
    State known;
    ReadAudio(known);
    if (std::none_of(known.outputs.begin(), known.outputs.end(), [&](const auto& o) { return o.id == id; })) return false;
    com_ptr<IPolicyConfig> policy;
    if (FAILED(CoCreateInstance(PolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), policy.put_void())) || !policy)
        return false;
    bool ok = true;
    for (const auto role : {eConsole, eMultimedia, eCommunications}) ok = SUCCEEDED(policy->SetDefaultEndpoint(id.c_str(), role)) && ok;
    return ok;
}
bool SetBrightness(const std::wstring& display, int percent) {
    percent = std::clamp(percent, 0, 100);
    for (auto& item : adjustable)
        if (item.device == display) {
            const bool ok = item.wmi ? WmiWrite(percent) : DdcWrite(display, percent);
            if (ok) item.level = percent;
            return ok;
        }
    return false;
}
bool OpenTrayOverflow(POINT anchor) {
    HWND overflow = nullptr;
    if (const HWND chevron = Win10Chevron()) {
        SendMessageTimeoutW(chevron, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 500, nullptr);
        overflow = FindWindowW(L"NotifyIconOverflowWindow", nullptr);
    } else {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            com_ptr<IUIAutomation> automation;
            const auto button = Win11Chevron(automation);
            com_ptr<IUIAutomationInvokePattern> invoke;
            if (!button || FAILED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(invoke.put()))) || !invoke ||
                FAILED(invoke->Invoke())) {
                if (SUCCEEDED(apartment)) CoUninitialize();
                // No hidden-icons button (every icon is pinned, or Explorer is
                // still starting): Win+B focuses the notification area instead.
                INPUT keys[4]{};
                for (auto& key : keys) key.type = INPUT_KEYBOARD;
                keys[0].ki.wVk = VK_LWIN;keys[1].ki.wVk = 'B';
                keys[2].ki.wVk = 'B';keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
                keys[3].ki.wVk = VK_LWIN;keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
                return SendInput(4, keys, sizeof(INPUT)) == 4;
            }
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        overflow = FindWindowW(L"TopLevelWindowForOverflowXamlIsland", nullptr);
    }
    if (!overflow) return false;
    // Wait for the flyout to appear, then move it above the control center.
    for (int i = 0; i < 40 && !IsWindowVisible(overflow); ++i) Sleep(15);
    if (!IsWindowVisible(overflow)) return false;
    RECT bounds{};
    GetWindowRect(overflow, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const auto& work = monitor.rcMonitor;
    const int left = std::clamp(static_cast<int>(anchor.x) - width / 2, static_cast<int>(work.left), static_cast<int>(work.right) - width);
    const int top = std::max(static_cast<int>(work.top), static_cast<int>(anchor.y) - height);
    SetWindowPos(overflow, HWND_TOPMOST, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(overflow);
    return true;
}
}  // namespace lume::platform::windows::control_center
