#pragma once
#include "core/notes/notes_store.h"
#include "core/settings/file_store.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lume::shell {
struct NotesSessionOptions {
    // Explicit UTF-8 filenames: the notes document and the desktop widget preferences.
    std::string notesPath, widgetPath;
    // Never shows the desktop widget, even for saved enabled preferences.
    bool verification = false;
    settings::IFileStore *files = nullptr;
    // UI-thread state changes from the widget itself (close, bounds, material).
    std::function<void(const std::wstring &)> notify = {};
    std::function<notes::Millis()> now = {};
    // Preview tools only: a separate WebView2 profile for the notes window.
    std::wstring webviewProfile;
};
// Owns the local notes document, the standalone notes window and the small
// desktop note. Pages edit notes only through scope "notes" messages.
class NotesSession final {
  public:
    NotesSession(HINSTANCE, NotesSessionOptions);
    ~NotesSession();
    NotesSession(const NotesSession &) = delete;
    NotesSession &operator=(const NotesSession &) = delete;
    bool Restore();
    // Strict version 1, scope "notes". Returns the replies to post, in order,
    // or nullopt for an unrelated or invalid message (no side effects).
    std::optional<std::vector<std::wstring>> HandleMessage(const std::wstring &);
    std::wstring State() const;
    // The small desktop note is visible.
    bool Running() const;
    // The desktop note or the notes window is open; the shell keeps pumping.
    bool Active() const;
    // Opens (or focuses) the full notes window. Never in verification.
    bool OpenWindow();
    HWND Handle() const;
    const notes::NotesState &Snapshot() const;
    void Shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lume::shell
