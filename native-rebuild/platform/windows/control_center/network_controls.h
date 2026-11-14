#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lume::platform::windows::control_center {
// Wi-Fi networks in range (strongest first, one row per SSID).
struct Network {
    std::wstring ssid;
    int signal = 0;              // 0-100
    bool secured = false, connected = false, known = false;  // known: a saved profile exists
};
std::vector<Network> Networks();
// Asks the adapter for a fresh scan; Networks() reflects it a few seconds later.
void ScanNetworks();
enum class ConnectResult { Started, NeedsPassword, Unsupported, Failed };
// Saved networks connect with their profile; open networks and secured ones
// with a password get a new per-user profile (WPA2/WPA3 personal).
ConnectResult Connect(const std::wstring& ssid, const std::wstring& password);
bool Disconnect();
// True once `ssid` is the connected network; on a failed new profile the
// profile is removed again so a wrong password is not remembered.
bool ConnectedTo(const std::wstring& ssid);
void ForgetCreatedProfile(const std::wstring& ssid);

// Paired Bluetooth devices. Windows exposes no public API to connect or
// disconnect them, so the panel lists them and opens Settings to manage.
struct Device { std::wstring name; bool connected = false; };
std::vector<Device> PairedDevices();

// The Windows accent color the panel uses for "on" (0xRRGGBB): the darker
// accent shade Windows uses for filled controls, default blue when unknown.
std::uint32_t AccentColor();
}  // namespace lume::platform::windows::control_center
