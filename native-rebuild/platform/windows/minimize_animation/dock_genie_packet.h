#pragma once
#include <windows.h>

namespace lume::platform::windows::dock_genie {
enum class Motion : DWORD { Minimize, Restore, Launch, Close, Zoom };
constexpr UINT CancelPlayback = WM_APP + 23;
constexpr wchar_t OverlayClass[] = L"LumeDockGenieOverlay";
constexpr wchar_t LastFrameProperty[] = L"Lume.Dock.Genie.FinalFrame";
constexpr ULONG_PTR SnapshotMessage = 0x4c474e31;
// WM_COPYDATA copies this header followed by tightly packed top-down BGRA.
struct Packet {
    RECT window;
    RECT destination;
    LONG edge;
    Motion motion;
};
}
