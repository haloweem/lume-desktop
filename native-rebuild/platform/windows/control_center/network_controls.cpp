#include "platform/windows/control_center/network_controls.h"
#include "platform/windows/silent_privacy.h"
#include <wlanapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>

namespace lume::platform::windows::control_center {
namespace {
// Profiles Lume created in this session, removed again if they never connect.
std::mutex createdMutex;
std::set<std::wstring> created;

struct Wlan {
    HANDLE handle = nullptr;
    GUID interfaceId{};
    bool ok = false;
    Wlan() {
        DWORD version = 0;
        if (WlanOpenHandle(2, nullptr, &version, &handle) != ERROR_SUCCESS) { handle = nullptr; return; }
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (WlanEnumInterfaces(handle, nullptr, &list) == ERROR_SUCCESS && list && list->dwNumberOfItems) {
            // Prefer the connected adapter, otherwise the first one.
            interfaceId = list->InterfaceInfo[0].InterfaceGuid;
            for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
                if (list->InterfaceInfo[i].isState == wlan_interface_state_connected) interfaceId = list->InterfaceInfo[i].InterfaceGuid;
            ok = true;
        }
        if (list) WlanFreeMemory(list);
    }
    ~Wlan() { if (handle) WlanCloseHandle(handle, nullptr); }
    Wlan(const Wlan&) = delete;
    Wlan& operator=(const Wlan&) = delete;
};
std::wstring Utf8Name(const DOT11_SSID& ssid) {
    const auto length = static_cast<int>(std::min<ULONG>(ssid.uSSIDLength, 32));
    const auto* bytes = reinterpret_cast<const char*>(ssid.ucSSID);
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, length, nullptr, 0);
    const UINT page = size > 0 ? CP_UTF8 : CP_ACP;
    size = MultiByteToWideChar(page, 0, bytes, length, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(page, 0, bytes, length, out.data(), size);
    return out;
}
struct Entry {
    Network network;
    DOT11_SSID raw{};
    DOT11_BSS_TYPE bss = dot11_BSS_type_infrastructure;
    DOT11_AUTH_ALGORITHM auth = DOT11_AUTH_ALGO_80211_OPEN;
    DOT11_CIPHER_ALGORITHM cipher = DOT11_CIPHER_ALGO_NONE;
    std::wstring profile;
};
std::vector<Entry> Entries(const Wlan& wlan) {
    std::vector<Entry> out;
    if (!HasExistingCapabilityAccess(L"wiFiControl")) return out;
    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    if (!wlan.ok || WlanGetAvailableNetworkList(wlan.handle, &wlan.interfaceId, 0, nullptr, &list) != ERROR_SUCCESS || !list) return out;
    std::map<std::wstring, std::size_t> bySsid;
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const auto& item = list->Network[i];
        if (!item.dot11Ssid.uSSIDLength || item.dot11BssType != dot11_BSS_type_infrastructure) continue;  // hidden / ad hoc
        Entry entry;
        entry.network.ssid = Utf8Name(item.dot11Ssid);
        entry.network.signal = static_cast<int>(item.wlanSignalQuality);
        entry.network.secured = item.bSecurityEnabled != FALSE;
        entry.network.connected = (item.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
        entry.network.known = (item.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0 && item.strProfileName[0];
        entry.raw = item.dot11Ssid;
        entry.bss = item.dot11BssType;
        entry.auth = item.dot11DefaultAuthAlgorithm;
        entry.cipher = item.dot11DefaultCipherAlgorithm;
        if (entry.network.known) entry.profile = item.strProfileName;
        const auto found = bySsid.find(entry.network.ssid);
        if (found == bySsid.end()) { bySsid[entry.network.ssid] = out.size();out.push_back(entry);continue; }
        auto& kept = out[found->second];
        // Merge duplicate rows: keep the profile row, the connection and the best signal.
        if (entry.network.known && !kept.network.known) { const bool c = kept.network.connected;const int s = kept.network.signal;kept = entry;kept.network.connected |= c;kept.network.signal = std::max(s, kept.network.signal); }
        else { kept.network.connected |= entry.network.connected;kept.network.signal = std::max(kept.network.signal, entry.network.signal); }
    }
    WlanFreeMemory(list);
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.network.connected != b.network.connected) return a.network.connected;
        return a.network.signal > b.network.signal;
    });
    if (out.size() > 40) out.resize(40);
    return out;
}
std::wstring Xml(const std::wstring& text) {
    std::wstring out;
    for (const auto c : text) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out += c;
        }
    }
    return out;
}
std::wstring Hex(const DOT11_SSID& ssid) {
    static const wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring out;
    for (ULONG i = 0; i < std::min<ULONG>(ssid.uSSIDLength, 32); ++i) {
        out += digits[ssid.ucSSID[i] >> 4];
        out += digits[ssid.ucSSID[i] & 15];
    }
    return out;
}
}  // namespace

std::vector<Network> Networks() {
    Wlan wlan;
    std::vector<Network> out;
    for (const auto& entry : Entries(wlan)) out.push_back(entry.network);
    return out;
}
void ScanNetworks() {
    if (!HasExistingCapabilityAccess(L"wiFiControl")) return;
    Wlan wlan;
    if (wlan.ok) WlanScan(wlan.handle, &wlan.interfaceId, nullptr, nullptr, nullptr);
}
ConnectResult Connect(const std::wstring& ssid, const std::wstring& password) {
    Wlan wlan;
    if (!wlan.ok) return ConnectResult::Failed;
    const auto entries = Entries(wlan);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.network.ssid == ssid; });
    if (found == entries.end()) return ConnectResult::Failed;
    std::wstring profile = found->profile;
    if (profile.empty()) {
        // A new profile: open networks, or personal (pre-shared key) security.
        std::wstring authentication, encryption;
        if (!found->network.secured) { authentication = L"open";encryption = L"none"; }
        else if (found->auth == DOT11_AUTH_ALGO_RSNA_PSK) authentication = L"WPA2PSK";
        else if (found->auth == DOT11_AUTH_ALGO_WPA_PSK) authentication = L"WPAPSK";
        else if (found->auth == static_cast<DOT11_AUTH_ALGORITHM>(9) /* WPA3 SAE */) authentication = L"WPA3SAE";
        else return ConnectResult::Unsupported;  // enterprise, WEP, OWE: Windows Settings
        if (encryption.empty()) encryption = found->cipher == DOT11_CIPHER_ALGO_TKIP ? L"TKIP" : L"AES";
        if (found->network.secured && (password.size() < 8 || password.size() > 63)) return ConnectResult::NeedsPassword;
        std::wstring xml = L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>" + Xml(ssid) +
                           L"</name><SSIDConfig><SSID><hex>" + Hex(found->raw) + L"</hex></SSID></SSIDConfig><connectionType>ESS</connectionType>"
                           L"<connectionMode>auto</connectionMode><MSM><security><authEncryption><authentication>" + authentication +
                           L"</authentication><encryption>" + encryption + L"</encryption><useOneX>false</useOneX></authEncryption>";
        if (found->network.secured)
            xml += L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + Xml(password) + L"</keyMaterial></sharedKey>";
        xml += L"</security></MSM></WLANProfile>";
        DWORD reason = 0;
        DWORD status = WlanSetProfile(wlan.handle, &wlan.interfaceId, 0, xml.c_str(), nullptr, TRUE, nullptr, &reason);
        if (status == ERROR_ACCESS_DENIED)  // all-user profiles can be restricted by policy
            status = WlanSetProfile(wlan.handle, &wlan.interfaceId, WLAN_PROFILE_USER, xml.c_str(), nullptr, TRUE, nullptr, &reason);
        if (status != ERROR_SUCCESS) return ConnectResult::Failed;
        profile = ssid;
        std::lock_guard lock(createdMutex);
        created.insert(ssid);
    }
    WLAN_CONNECTION_PARAMETERS parameters{};
    parameters.wlanConnectionMode = wlan_connection_mode_profile;
    parameters.strProfile = profile.c_str();
    parameters.dot11BssType = found->bss;
    return WlanConnect(wlan.handle, &wlan.interfaceId, &parameters, nullptr) == ERROR_SUCCESS ? ConnectResult::Started : ConnectResult::Failed;
}
bool Disconnect() {
    Wlan wlan;
    return wlan.ok && WlanDisconnect(wlan.handle, &wlan.interfaceId, nullptr) == ERROR_SUCCESS;
}
bool ConnectedTo(const std::wstring& ssid) {
    for (const auto& network : Networks())
        if (network.connected && network.ssid == ssid) {
            std::lock_guard lock(createdMutex);
            created.erase(ssid);
            return true;
        }
    return false;
}
void ForgetCreatedProfile(const std::wstring& ssid) {
    {
        std::lock_guard lock(createdMutex);
        if (!created.erase(ssid)) return;
    }
    Wlan wlan;
    if (wlan.ok) WlanDeleteProfile(wlan.handle, &wlan.interfaceId, ssid.c_str(), nullptr);
}
static std::vector<Device> EnumerateBluetooth(bool includeUnpaired) {
    using namespace winrt::Windows::Devices;
    std::vector<Device> out;
    std::set<std::wstring> seen;
    const auto add = [&](const winrt::hstring& selector, bool paired) {
        try {
            const auto devices = Enumeration::DeviceInformation::FindAllAsync(selector, {L"System.Devices.Aep.IsConnected"},
                                                                              Enumeration::DeviceInformationKind::AssociationEndpoint).get();
            for (const auto& device : devices) {
                std::wstring name(device.Name());
                if (name.empty() || (!paired && !device.Pairing().CanPair())) continue;
                if (!seen.insert(std::wstring(device.Id())).second) continue;
                bool connected = false;
                if (const auto value = device.Properties().TryLookup(L"System.Devices.Aep.IsConnected"))
                    connected = winrt::unbox_value_or<bool>(value, false);
                out.push_back({std::move(name), connected, std::wstring(device.Id()), paired});
            }
        } catch (...) {}
    };
    add(Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), true);
    add(Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), true);
    if (includeUnpaired) {
        add(Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(false), false);
        add(Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(false), false);
    }
    std::stable_sort(out.begin(), out.end(), [](const Device& a, const Device& b) {
        return a.connected != b.connected ? a.connected : a.paired && !b.paired;
    });
    if (out.size() > 30) out.resize(30);
    return out;
}
std::vector<Device> PairedDevices() { return EnumerateBluetooth(false); }
std::vector<Device> BluetoothDevices() { return EnumerateBluetooth(true); }
bool PairDevice(const std::wstring& id) {
    using namespace winrt::Windows::Devices::Enumeration;
    try {
        const auto device = DeviceInformation::CreateFromIdAsync(id, nullptr, DeviceInformationKind::AssociationEndpoint).get();
        if (!device) return false;
        const auto pairing = device.Pairing();
        if (pairing.IsPaired()) return true;
        if (!pairing.CanPair()) return false;
        const auto status = pairing.PairAsync().get().Status();
        return status == DevicePairingResultStatus::Paired || status == DevicePairingResultStatus::AlreadyPaired;
    } catch (...) { return false; }
}
std::uint32_t AccentColor() {
    try {
        const auto color = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
        return (static_cast<std::uint32_t>(color.R) << 16) | (static_cast<std::uint32_t>(color.G) << 8) | color.B;
    } catch (...) { return 0x0067c0; }
}
}  // namespace lume::platform::windows::control_center
