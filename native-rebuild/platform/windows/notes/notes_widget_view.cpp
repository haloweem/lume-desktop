#include "platform/windows/notes/notes_widget_view.h"
#include <algorithm>

namespace lume::platform::windows {
namespace {
bool High(wchar_t c) { return c >= 0xd800 && c <= 0xdbff; }
// Largest prefix length of text[from..] fitting width, at least one character.
std::size_t Fit(WidgetCanvas &c, const std::wstring &text, std::size_t from, std::size_t to, float width, float size) {
    std::size_t low = 1, high = to - from, best = 1;
    while (low <= high) {
        const auto mid = (low + high) / 2;
        if (c.MeasureText(text.substr(from, mid), size) <= width) {
            best = mid;
            low = mid + 1;
        } else
            high = mid - 1;
    }
    if (from + best < to && High(text[from + best - 1]))
        best = best > 1 ? best - 1 : best + 1;
    return std::min(best, to - from);
}
} // namespace
void NotesWidgetView::Set(std::wstring title, std::wstring body, std::wstring date, bool any) {
    title_ = std::move(title);
    body_ = std::move(body);
    date_ = std::move(date);
    any_ = any;
}
std::vector<std::wstring> NotesWidgetView::Wrap(WidgetCanvas &c, const std::wstring &text, float width, float size,
                                                std::size_t maxLines) {
    std::vector<std::wstring> lines;
    if (width <= 1 || !maxLines)
        return lines;
    std::size_t at = 0;
    bool truncated = false;
    while (at <= text.size()) {
        auto end = text.find(L'\n', at);
        if (end == std::wstring::npos)
            end = text.size();
        auto start = at;
        if (start == end) {
            // Blank lines are kept only between paragraphs, never at the top.
            if (!lines.empty() && !lines.back().empty())
                lines.emplace_back();
        }
        while (start < end) {
            if (lines.size() == maxLines) {
                truncated = true;
                break;
            }
            const auto count = Fit(c, text, start, end, width, size);
            lines.push_back(text.substr(start, count));
            start += count;
        }
        if (truncated || end >= text.size())
            break;
        if (lines.size() == maxLines) {
            truncated = true;
            break;
        }
        at = end + 1;
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    if (truncated && !lines.empty()) {
        auto &last = lines.back();
        while (!last.empty() && c.MeasureText(last + L"…", size) > width) {
            last.pop_back();
            if (!last.empty() && High(last.back()))
                last.pop_back();
        }
        last += L"…";
    }
    return lines;
}
void NotesWidgetView::Draw(WidgetCanvas &c, float width, float height) {
    const float pad = 18, badge = 22;
    c.Text(L"便签", {pad, 16, 80, badge}, 13, 0xffffff, .85f);
    if (!date_.empty())
        c.Text(date_, {width - pad - 120, 16, 120, badge}, 12, 0xffffff, .6f, false, WidgetTextAlign::Right);
    const float inner = std::max(1.f, width - pad * 2);
    if (!any_) {
        c.Text(L"还没有便签", {pad, 54, inner, 24}, 17, 0xffffff, .9f, WidgetTextWeight::Semibold);
        c.Text(L"双击编辑，写下第一条", {pad, 84, inner, 20}, 13, 0xffffff, .65f);
        return;
    }
    const float titleSize = 17;
    const auto title = Wrap(c, title_.empty() ? std::wstring(L"无标题笔记") : title_, inner, titleSize, 1);
    c.Text(title.empty() ? std::wstring{} : title.front(), {pad, 50, inner, 26}, titleSize, 0xffffff, 1,
           WidgetTextWeight::Semibold);
    const float bodySize = 13, lineHeight = 21, top = 84;
    const auto available = static_cast<std::size_t>(std::max(0.f, (height - top - 14) / lineHeight));
    const auto lines = Wrap(c, body_.empty() ? std::wstring(L"空白笔记") : body_, inner, bodySize, available);
    for (std::size_t i = 0; i < lines.size(); ++i)
        c.Text(lines[i], {pad, top + i * lineHeight, inner, lineHeight}, bodySize, 0xffffff, body_.empty() ? .5f : .82f);
}
} // namespace lume::platform::windows
