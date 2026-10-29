#pragma once
#include <windows.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lume::platform::windows {
struct OrganizerDropFeedback {
    enum class Phase { Enter, Over, Leave };
    Phase phase;
    POINT point;
    std::size_t count;
    bool supported;
};

// Attach、Detach 和析构必须在 HWND 所属线程执行；回调也在此线程执行。
// point 为客户区物理像素，路径为 UTF-8；这里只导入引用，不操作文件。
class OrganizerDropTarget final {
public:
    using Feedback = std::function<void(const OrganizerDropFeedback&)>;
    using Drop = std::function<bool(const std::vector<std::string>&, POINT)>;
    OrganizerDropTarget();
    ~OrganizerDropTarget();
    OrganizerDropTarget(const OrganizerDropTarget&) = delete;
    OrganizerDropTarget& operator=(const OrganizerDropTarget&) = delete;
    // shellLocations: virtual shell items without a file (回收站, 此电脑, 控制面板)
    // are accepted as shortcuts from ShellLocationShortcut; otherwise skipped.
    bool Attach(HWND, Feedback, Drop, bool shellLocations = false);
    void Detach();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Creates or refreshes %LOCALAPPDATA%\Lume\shell-items\<name>.lnk pointing at a
// shell location given by parsing name (for example "::{645FF040-...}" for the
// Recycle Bin). The shortcut opens and draws like the location itself, so it
// can be pinned like any file. Returns its normalized UTF-8 path. Needs COM.
std::optional<std::string> ShellLocationShortcut(const std::wstring& parsingName);
}
