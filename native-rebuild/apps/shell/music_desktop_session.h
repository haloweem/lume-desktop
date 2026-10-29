#pragma once

#include "core/settings/file_store.h"
#include "core/ui/music_page_protocol.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lume::shell {

struct MusicDesktopSessionOptions {
    // Explicit UTF-8 filename, normally <settings directory>/music-widget.json.
    // No default user-profile lookup or directory creation.
    std::string path;
    // Suppresses Show and SMTC registration, even for saved enabled preferences.
    bool verification = false;
    // Optional test adapter, which must outlive the session.
    settings::IFileStore* files = nullptr;
    // UI-thread State() changes only, including native close/bounds and failures.
    // Posts a snapshot (result="snapshot", saved=false), not a command ACK.
    // No notification on destruction; call Shutdown explicitly while the parent lives.
    std::function<void(const std::wstring&)> notify = {};
    // Worker completion posts to the shell message pump; never drains from the worker.
    std::function<void()> wake = {};
};

class MusicDesktopSession final {
public:
    using CommandHandler = std::function<void(const ui::MusicPageCommand&)>;

    // UI-thread only; parent owns the COM apartment and ordinary message pump.
    // Callbacks may Update/Shutdown, but must defer destruction of this object.
    MusicDesktopSession(HINSTANCE, MusicDesktopSessionOptions, CommandHandler,
                        std::function<void()> openSettings);
    ~MusicDesktopSession();
    MusicDesktopSession(const MusicDesktopSession&) = delete;
    MusicDesktopSession& operator=(const MusicDesktopSession&) = delete;

    // Idempotent. Production registers SMTC on its own invisible top-level HWND,
    // independently of desktop widget visibility or the manager window lifetime.
    // False reports load/runtime/SMTC failure; State describes partial success.
    bool Restore();
    void Update(const music::MusicSnapshot&);
    // Strict version 1, scope "music-desktop", snapshot or setEnabled + bool value.
    // Invalid/unrelated messages return nullopt without side effects.
    // Call Restore before setEnabled. State/snapshot remain available after stop.
    std::optional<std::wstring> HandleMessage(const std::wstring&);
    std::wstring State() const;
    bool Running() const;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lume::shell
