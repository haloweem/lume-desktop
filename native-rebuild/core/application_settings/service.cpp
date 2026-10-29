#include "core/application_settings/application_settings.h"
#include <utility>
namespace lume::application_settings {
Service::Service(IPreferencesFile& file, IStartupRegistration& startup, std::wstring executable,
                 std::function<bool(bool)> apply, const Cancellation* cancellation, bool restoreStartup)
    : file_(file), startup_(startup), executable_(std::move(executable)), command_(StartupCommand(executable_)),
      applyBalancedMode_(std::move(apply)), cancellation_(cancellation), restoreStartup_(restoreStartup) {}
void Service::RefreshError() {
    state_.error = !balancedRuntimeError_.empty() ? balancedRuntimeError_
        : !startupRuntimeError_.empty() ? startupRuntimeError_ : operationError_;
}
void Service::Fail(const char* error) { operationError_ = error; RefreshError(); ++state_.revision; }
bool Service::ReadStartup(StartupEntry& entry) {
    try {
        if (command_ && startup_.Read(entry)) return true;
    } catch (...) {}
    startupRuntimeError_ = command_ ? "autostart-read" : "autostart-command";
    Fail(startupRuntimeError_.c_str());
    return false;
}
void Service::ObserveStartup(const StartupEntry& entry) {
    const auto kind = ClassifyStartupEntry(entry, executable_);
    state_.preferences.autostart = kind == StartupEntryKind::Current || kind == StartupEntryKind::Legacy ||
                                  kind == StartupEntryKind::OtherInstallation;
    startupRuntimeError_ = kind == StartupEntryKind::Foreign || kind == StartupEntryKind::OtherInstallation
        ? "autostart-conflict" : "";
    RefreshError();
}
bool Service::LoadPreferences() {
    restored_ = true;
    writable_ = false;
    initializeAutostart_ = false;
    state_.preferences = {};
    std::string storageError;
    try {
        const auto loaded = file_.Read();
        if (loaded.status == ReadStatus::Failed) storageError = "preferences-read";
        else if (loaded.status == ReadStatus::Missing) initializeAutostart_ = true;
        else if (loaded.status == ReadStatus::Loaded) {
            const auto decoded = DecodePreferences(loaded.bytes);
            storageError = decoded.error;
            if (storageError.empty()) state_.preferences = decoded.preferences;
        }
        writable_ = storageError.empty();
    } catch (...) { storageError = "preferences-read"; }
    storageError_ = storageError;
    autostartIntent_ = state_.preferences.autostart;
    operationError_ = storageError;
    RefreshError();
    ++state_.revision;
    return writable_;
}
bool Service::Load() {
    LoadPreferences();
    StartupEntry entry;
    const bool startupOk = ReadStartup(entry);
    if (startupOk) ObserveStartup(entry);
    if (!storageError_.empty()) Fail(storageError_.c_str());
    else if (startupOk) { operationError_.clear(); RefreshError(); ++state_.revision; }
    return writable_ && startupOk;
}
bool Service::ApplyBalanced(bool enabled) noexcept {
    try { return applyBalancedMode_ && applyBalancedMode_(enabled); }
    catch (...) { return false; }
}
bool Service::Restore() {
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    LoadPreferences();
    StartupEntry entry;
    bool startupOk = ReadStartup(entry);
    if (startupOk) ObserveStartup(entry);
    if (!writable_) { Fail(storageError_.c_str()); return false; }
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    if (startupOk && restoreStartup_) {
        const auto kind = ClassifyStartupEntry(entry, executable_);
        if (kind == StartupEntryKind::Foreign || kind == StartupEntryKind::OtherInstallation) {
            Fail("autostart-conflict");
            startupOk = false;
        } else if (initializeAutostart_ || kind == StartupEntryKind::Legacy ||
                   (!autostartIntent_ && kind == StartupEntryKind::Current)) {
            startupOk = ApplyAutostart(autostartIntent_, true);
        }
    }
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    if (!ApplyBalanced(state_.preferences.balancedMode)) {
        if (balancedRuntimeError_.empty()) balancedRuntimeError_ = "balanced-runtime";
        Fail("balanced-runtime");
        return false;
    }
    balancedRuntimeError_.clear();
    if (startupOk) operationError_.clear();
    RefreshError();
    ++state_.revision;
    return state_.error.empty();
}
bool Service::Ready() {
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    if (!restored_) { Fail("not-restored"); return false; }
    if (!writable_) { Fail(storageError_.empty() ? "preferences-read" : storageError_.c_str()); return false; }
    if (state_.busy) { Fail("busy"); return false; }
    return true;
}
bool Service::Save(const Preferences& preferences, bool saveAutostart, bool restoring) {
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    auto persisted = preferences;
    if (!saveAutostart) persisted.autostart = autostartIntent_;
    try {
        // 保存前重读，保护其他进程的新版本配置及首次注册期间出现的显式意图。
        const auto current = file_.Read();
        if (current.status == ReadStatus::Failed) { Fail("preferences-read"); return false; }
        if (current.status == ReadStatus::Loaded) {
            const auto decoded = DecodePreferences(current.bytes);
            if (!decoded.error.empty()) {
                writable_ = false;
                storageError_ = decoded.error;
                Fail(decoded.error.c_str());
                return false;
            }
            if (restoring &&
                (initializeAutostart_ ||
                 decoded.preferences.language != preferences.language ||
                 decoded.preferences.balancedMode != preferences.balancedMode ||
                 decoded.preferences.autostart != autostartIntent_)) {
                Fail("preferences-save");
                return false;
            }
            if (!saveAutostart) persisted.autostart = decoded.preferences.autostart;
        } else if (restoring && !initializeAutostart_) {
            Fail("preferences-save");
            return false;
        }
        if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
        if (!file_.WriteAtomically(EncodePreferences(persisted))) { Fail("preferences-save"); return false; }
    } catch (...) { Fail("preferences-save"); return false; }
    const bool actualAutostart = state_.preferences.autostart;
    state_.preferences = preferences;
    state_.preferences.autostart = actualAutostart;
    autostartIntent_ = persisted.autostart;
    if (saveAutostart) initializeAutostart_ = false;
    operationError_.clear();
    RefreshError();
    ++state_.revision;
    return true;
}
bool Service::SetLanguage(const std::string& language) {
    if (!Ready()) return false;
    if (language != "zh-CN" && language != "en") { Fail("language-invalid"); return false; }
    auto next = state_.preferences;
    next.language = language;
    return Save(next);
}
bool Service::SetBalancedMode(bool enabled) {
    if (!Ready()) return false;
    const bool previous = state_.preferences.balancedMode;
    if (!ApplyBalanced(enabled)) {
        const bool rolledBack = ApplyBalanced(previous);
        balancedRuntimeError_ = rolledBack ? "" : "balanced-rollback";
        Fail(rolledBack ? "balanced-runtime" : "balanced-rollback");
        return false;
    }
    auto next = state_.preferences;
    next.balancedMode = enabled;
    if (Save(next)) {
        balancedRuntimeError_.clear();
        RefreshError();
        return true;
    }
    if (ApplyBalanced(previous)) {
        balancedRuntimeError_.clear();
        RefreshError();
    } else {
        balancedRuntimeError_ = "balanced-rollback";
        Fail("balanced-rollback");
    }
    return false;
}
bool Service::RollbackStartup(const StartupEntry& entry, const StartupEntry& expected) {
    try {
        StartupEntry actual;
        if (!startup_.Read(actual)) return false;
        ObserveStartup(actual);
        // 外部已改变本次写入结果时，不得用旧快照重新覆盖。
        if (!SameStartupEntry(expected, actual) && !SameStartupEntry(entry, actual)) return false;
        if (SameStartupEntry(entry, actual)) return true;
        const auto kind = ClassifyStartupEntry(actual, executable_);
        if (kind == StartupEntryKind::Foreign || kind == StartupEntryKind::OtherInstallation) return false;
        bool written = false;
        try { written = startup_.Write(entry); } catch (...) {}
        const bool read = startup_.Read(actual);
        if (read) ObserveStartup(actual);
        return written && read && SameStartupEntry(entry, actual);
    } catch (...) { return false; }
}
bool Service::SetAutostart(bool enabled) {
    if (!Ready()) return false;
    return ApplyAutostart(enabled, false);
}
bool Service::SetWallpaperDirectory(const std::string& directory) {
    if (!Ready()) return false;
    if (directory.size() > 260) { Fail("wallpaper-directory-invalid"); return false; }
    for (const auto c : directory)
        if (static_cast<unsigned char>(c) < 32) { Fail("wallpaper-directory-invalid"); return false; }
    auto next = state_.preferences;
    next.wallpaperDirectory = directory;
    return Save(next);
}
bool Service::ApplyAutostart(bool enabled, bool restoring) {
    StartupEntry previous;
    if (!ReadStartup(previous)) return false;
    ObserveStartup(previous);
    const auto kind = ClassifyStartupEntry(previous, executable_);
    if (kind == StartupEntryKind::Foreign || kind == StartupEntryKind::OtherInstallation) {
        Fail("autostart-conflict");
        return false;
    }
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    const auto desired = enabled ? EnabledStartupEntry(*command_) : StartupEntry{};
    bool applied = SameStartupEntry(previous, desired);
    if (!applied) {
        bool written = false;
        try { written = startup_.Write(desired); } catch (...) {}
        StartupEntry actual;
        try {
            const bool read = startup_.Read(actual);
            if (read) ObserveStartup(actual);
            applied = written && read && SameStartupEntry(actual, desired);
        } catch (...) {}
    }
    if (!applied) {
        const bool rolledBack = RollbackStartup(previous, desired);
        if (!rolledBack) startupRuntimeError_ = "autostart-rollback";
        Fail(rolledBack ? "autostart-write" : "autostart-rollback");
        return false;
    }
    auto next = state_.preferences;
    next.autostart = enabled;
    if (Save(next, true, restoring)) return true;
    if (!RollbackStartup(previous, desired)) {
        startupRuntimeError_ = "autostart-rollback";
        Fail("autostart-rollback");
    }
    return false;
}
bool Service::BeginCheck() {
    if (state_.busy) return false;
    if (cancellation_ && cancellation_->cancelled) { Fail("cancelled"); return false; }
    compatibleUpdate_ = false;
    state_.busy = true;
    operationError_.clear();
    RefreshError();
    state_.update = {"checking", {}, 0};
    ++state_.revision;
    return true;
}
void Service::FinishCheck(const ManifestResult& result) {
    state_.busy = false;
    state_.update = result.update;
    operationError_ = result.error;
    compatibleUpdate_ = (result.update.status == "available" && result.error.empty()) ||
                        (result.update.status == "incompatible" && result.error == "update-disabled");
    RefreshError();
    ++state_.revision;
}
void Service::UnsupportedUpdate() {
    state_.update.status = "incompatible";
    if (compatibleUpdate_) {
        Fail("update-disabled");
    } else {
        Fail("update-incompatible");
    }
}
void Service::UpdateProgress(UpdateState update, std::string error) {
    state_.busy = update.status == "downloading";
    state_.update = std::move(update);
    operationError_ = std::move(error);
    RefreshError();
    ++state_.revision;
}
void Service::CancelCheck() {
    if (!state_.busy) return;
    state_.busy = false;
    state_.update.status = "error";
    Fail("update-cancelled");
}
const Snapshot& Service::State() const noexcept { return state_; }
}
