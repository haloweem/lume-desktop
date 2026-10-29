#pragma once
#include "core/dock/dock_model.h"
#include <windows.h>
#include <string>
#include <vector>

namespace lume::platform::windows::dock_sources {
// Case-insensitive path identity and NFKC lowercase search keys.
std::string PathKey(std::string_view);
std::string SearchKey(std::string_view);
// Files and folders on the user and public desktops, excluding hidden and
// system entries such as desktop.ini. Paths are normalized UTF-8.
std::vector<std::string> DesktopPaths();
// The program a shortcut starts, or the path itself for anything else.
std::string ShortcutTarget(const std::string& path);
// Programs with a taskbar-style window (visible, unowned, not a tool window,
// not cloaked), excluding this process and Explorer's shell windows.
std::vector<dock::RunningApp> RunningApps();
// Restores and focuses the program's windows; a program already in front is
// minimized, and one with several windows cycles to the next. Returns false
// when the program has no window (the caller starts it instead).
bool ActivateProgram(const std::string& executable);
// Taskbar-style windows in first-seen order, each marked minimized or not.
std::vector<dock::ProgramWindow> ProgramWindows();
// Restores and focuses a window; a window already in front is minimized.
bool ActivateWindow(std::uint64_t handle);
// True when `path` is the Recycle Bin, or a shortcut to it.
bool IsRecycleBin(const std::string& path);
// Moves files and folders to the Recycle Bin with the shell's own progress
// and confirmation behaviour. Returns false when nothing was recycled.
bool Recycle(HWND owner, const std::vector<std::string>& paths);
// Registers the desktop folders for shell change notifications to `window`.
ULONG WatchDesktop(HWND window, UINT message);
void StopWatching(ULONG registration);
}  // namespace lume::platform::windows::dock_sources
