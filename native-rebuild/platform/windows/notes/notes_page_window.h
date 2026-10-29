#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include "core/widget/widget_preferences.h"

namespace lume::platform::windows {
struct NotesPageOptions {
    std::string noteId;
    widget::Preferences preferences;
    bool edit = false;
    std::function<void(UINT)> menu;
    std::function<void(widget::Preferences)> boundsChanged;
};
// The standalone notes window: a resizable top-level window with an acrylic
// backdrop hosting ui/notes.html. It has no message loop of its own; the
// shell's UI thread pumps it, with or without the manager window open.
class NotesPageWindow final {
  public:
    // Trusted page messages, delivered after the WebView2 event returns.
    using MessageHandler = std::function<void(const std::wstring &)>;
    NotesPageWindow();
    ~NotesPageWindow();
    NotesPageWindow(const NotesPageWindow &) = delete;
    NotesPageWindow &operator=(const NotesPageWindow &) = delete;
    // Creates the window, or restores and focuses an open one.
    // An empty profile uses the manager's WebView2 profile (shared browser process).
    bool Open(HINSTANCE, MessageHandler, std::function<void()> closed, const std::wstring &profile = {}, NotesPageOptions options = {});
    void Configure(const widget::Preferences &);
    widget::Preferences Bounds() const;
    // Asks the page to finish saving, then closes. Save failure keeps it open.
    void RequestClose();
    void CancelClose();
    // Immediate close without the save handshake (application exit).
    void Close();
    // Dropped until the page has loaded; the page requests a snapshot itself.
    void Post(const std::wstring &json);
    bool IsOpen() const;
    HWND Handle() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace lume::platform::windows
