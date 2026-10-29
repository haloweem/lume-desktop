#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <string>
#include <vector>

namespace lume::platform::windows {
// Foreground of the desktop note: the shown note's title, date and wrapped body.
class NotesWidgetView {
  public:
    // UTF-16 text; an empty title with an empty body draws the empty hint.
    void Set(std::wstring title, std::wstring body, std::wstring date, bool any);
    void Draw(WidgetCanvas &, float width, float height);
    // Greedy wrap by measured width, never splitting a surrogate pair. Explicit
    // newlines start new lines; at most maxLines, the last one ellipsized.
    static std::vector<std::wstring> Wrap(WidgetCanvas &, const std::wstring &text, float width, float size,
                                          std::size_t maxLines);

  private:
    std::wstring title_, body_, date_;
    bool any_ = false;
};
} // namespace lume::platform::windows
