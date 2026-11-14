#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <vector>

namespace lume::platform::windows::control_center {
// Every control reports whether this computer has it; the panel hides or
// explains what is missing instead of failing. All functions block (DDC/CI,
// WinRT radios, tethering) and belong on a worker thread with COM initialized.
struct Choice { std::wstring id, name; };
struct Radio { bool available = false, on = false; };
struct State {
    Radio wifi, bluetooth;
    std::wstring ssid;            // connected Wi-Fi network, empty when none
    int signal = 0;               // 0-100
    bool ethernet = false;        // a wired connection is up
    // Mobile hotspot. available=false: reason explains (no network, unsupported).
    bool hotspotAvailable = false, hotspotOn = false;
    std::wstring hotspotReason;
    // Lume's airplane mode: every radio off. Windows has no public API for the
    // system airplane switch; available needs at least one radio.
    bool airplaneAvailable = false, airplaneOn = false;
    bool audioAvailable = false, muted = false;
    int volume = 0;               // 0-100
    std::wstring output;          // default render endpoint id
    std::vector<Choice> outputs;
    bool brightnessAvailable = false;
    int brightness = 0;           // 0-100 of the selected display
    std::wstring display;         // GDI device name, for example \\.\DISPLAY1
    std::vector<Choice> displays; // only displays whose brightness can be set
    bool batteryAvailable = false, charging = false;
    int battery = 0;
    bool trayAvailable = false;   // the taskbar notification overflow can be opened
};
// display: selected display (empty picks the first adjustable one).
// readBrightness=false keeps the cached value (DDC/CI reads are slow).
State Read(const std::wstring& display, bool readBrightness);
bool SetRadio(bool wifi, bool on);
bool SetAirplane(bool on);
bool SetHotspot(bool on);
bool SetVolume(int percent);
bool SetMute(bool muted);
bool SetOutput(const std::wstring& id);
bool SetBrightness(const std::wstring& display, int percent);
// Opens the taskbar's notification-icon overflow and moves it so its bottom
// edge sits at `anchor` (screen point, horizontally centred). Runs on any thread.
bool OpenTrayOverflow(POINT anchor);
}  // namespace lume::platform::windows::control_center
