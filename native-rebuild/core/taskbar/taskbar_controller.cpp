#include "core/taskbar/taskbar_controller.h"
#include <utility>

namespace lume::taskbar {
Controller::Controller(RecordStore& store, std::string path, ITaskbarPort& port)
    : store_(store), path_(std::move(path)), port_(port) {
    const auto loaded = store_.Load(path_);
    record_ = loaded.record; loaded_ = loaded.state;
    initialRecovery_ = HasRecovery(record_);
    if (loaded_ == LoadState::Failed) error_ = Error::Load;
}
Preferences Controller::Effective() const {
    auto value = record_.preferences;
    if (!autoHideDemand_) return value;
    // Hiding only: a disabled taskbar module must not start transparency.
    if (!value.enabled) { value.enabled = true; value.transparentTaskbar = false; }
    value.autoHideTaskbar = true;
    return value;
}
bool Controller::Fail(Error error) {
    error_ = error; running_ = false;
    if (acquired_ && !HasRecovery(record_)) {
        port_.Release(); acquired_ = false; transparencyAcquired_ = false;
    }
    return false;
}
bool Controller::Acquire(bool transparency) {
    const bool first = !acquired_;
    const auto error = port_.Acquire(transparency, mode_);
    if (error != Error::None) return Fail(error);
    acquired_ = true; transparencyAcquired_ = transparencyAcquired_ || transparency;
    if (first) {
        // Reload under the cross-process ownership lock. A second instance must
        // never overwrite an active instance's journal with its stale snapshot.
        const auto latest = store_.Load(path_);
        if (latest.state == LoadState::Failed) {
            loaded_ = LoadState::Failed;
            port_.Release(); acquired_ = false; transparencyAcquired_ = false;
            return Fail(Error::Load);
        }
        record_ = latest.record; loaded_ = latest.state;
        initialRecovery_ = HasRecovery(record_);
    }
    return true;
}
bool Controller::Save(Record next) {
    if (!store_.Save(path_, next)) return Fail(Error::Save);
    record_ = std::move(next); loaded_ = LoadState::Loaded;
    return true;
}
bool Controller::Apply(Preferences preferences) {
    if (loaded_ == LoadState::Failed) return Fail(Error::Load);
    if (!(record_.preferences == preferences)) {
        if (!acquired_ && !Acquire(preferences.enabled && preferences.transparentTaskbar)) return false;
        auto next = record_; next.preferences = preferences;
        if (!Save(std::move(next))) return false;
    }
    return Tick();
}
bool Controller::RestoreOwned() {
    running_ = false;
    if (!HasRecovery(record_)) {
        if (acquired_) { port_.Release(); acquired_ = false; transparencyAcquired_ = false; }
        return true;
    }
    if (!acquired_) {
        // Taking ownership must not depend on a usable transparency payload:
        // automatic hiding and classic appearance have independent recovery.
        if (!Acquire(false)) return false;
    }
    Error failure = Error::None;
    const auto remember = [&](Error error) { if (failure == Error::None) failure = error; };
    // Stop the core first: its normal shutdown restores Explorer's appearance.
    // A failed item retains its journal entry, but does not block other items.
    if (record_.ownsCore) {
        // StopCore validates any recovery launch itself and deliberately ignores
        // enable-request cancellation. A live owned core needs no payload reload.
        const auto error = port_.StopCore(!startedCore_);
        if (error != Error::None) remember(error);
        else {
            startedCore_ = false;
            auto next = record_; next.ownsCore = false;
            if (!Save(std::move(next))) remember(error_);
        }
    }
    for (std::size_t i = 0; i < record_.appearances.size();) {
        if (port_.RestoreAppearance(record_.appearances[i]) == RestoreResult::Failed) {
            remember(Error::Recovery); ++i; continue;
        }
        auto next = record_; next.appearances.erase(next.appearances.begin() + static_cast<std::ptrdiff_t>(i));
        if (!Save(std::move(next))) { remember(error_); ++i; }
    }
    // Visibility recovery is independent of a missing or stalled transparency core.
    if (!ApplyVisibility(false)) remember(error_);
    if (record_.ownsAutoHide) {
        bool current = false;
        auto error = port_.ReadAutoHide(current);
        if (error == Error::None && current != record_.originalAutoHide)
            error = port_.SetAutoHide(record_.originalAutoHide);
        if (error != Error::None) remember(error);
        else {
            auto next = record_; next.ownsAutoHide = false;
            if (!Save(std::move(next))) remember(error_);
        }
    }
    if (failure != Error::None) return Fail(failure);
    port_.Release(); acquired_ = false; transparencyAcquired_ = false;
    return true;
}
void Controller::RepairOrphans() {
    const auto p = Effective();
    if (p.enabled && p.autoHideTaskbar && p.hideBehavior == HideBehavior::FullyHidden) return;
    if (record_.hiddenTaskbars.empty() && !orphansRepaired_) { port_.RepairHidden(); orphansRepaired_ = true; }
}
bool Controller::ApplyVisibility(bool fullyHidden) {
    if (!fullyHidden) {
        bool ok = true;
        for (std::size_t i = 0; i < record_.hiddenTaskbars.size();) {
            if (port_.RestoreVisibility(record_.hiddenTaskbars[i]) == RestoreResult::Failed) {
                ok = false; ++i; continue;
            }
            auto next = record_;
            next.hiddenTaskbars.erase(next.hiddenTaskbars.begin() + static_cast<std::ptrdiff_t>(i));
            if (!Save(std::move(next))) return false;
        }
        // Repair orphans once whenever full hiding ends (or Lume starts), not on
        // every maintenance tick, so another tool's own hiding is left alone.
        if (ok && !orphansRepaired_) { port_.RepairHidden(); orphansRepaired_ = true; }
        return ok ? true : Fail(Error::Recovery);
    }
    std::vector<Visibility> windows;
    auto error = port_.ReadVisibility(windows);
    if (error != Error::None) return Fail(error);
    auto next = record_;
    for (auto it = next.hiddenTaskbars.begin(); it != next.hiddenTaskbars.end();) {
        bool found = false;
        for (const auto& window : windows) if (SameTarget(*it, window)) { found = true; break; }
        if (found) ++it; else it = next.hiddenTaskbars.erase(it);
    }
    bool changed = next.hiddenTaskbars.size() != record_.hiddenTaskbars.size();
    for (const auto& window : windows) {
        bool found = false;
        for (const auto& original : next.hiddenTaskbars) if (SameTarget(original, window)) { found = true; break; }
        if (!found) { next.hiddenTaskbars.push_back(window); changed = true; }
    }
    if (changed && !Save(std::move(next))) return false;
    orphansRepaired_ = false;  // Repair again when full hiding later ends.
    for (const auto& window : record_.hiddenTaskbars)
        if ((error = port_.Hide(window)) != Error::None) return Fail(error);
    return true;
}
bool Controller::Tick() {
    if (loaded_ == LoadState::Failed) return Fail(Error::Load);
    if (initialRecovery_) {
        if (!RestoreOwned()) return false;
        initialRecovery_ = false;
    }
    const auto preferences = Effective();
    if (!preferences.enabled) {
        if (!RestoreOwned()) return false;
        error_ = Error::None; return true;
    }
    if (!acquired_ || (preferences.transparentTaskbar && !transparencyAcquired_)) {
        if (!Acquire(preferences.transparentTaskbar)) return false;
        if (initialRecovery_) {
            if (!RestoreOwned()) return false;
            initialRecovery_ = false;
            return Tick();
        }
    }
    bool current = false;
    auto error = port_.ReadAutoHide(current);
    if (error != Error::None) return Fail(error);
    if (!record_.ownsAutoHide) {
        auto next = record_; next.ownsAutoHide = true; next.originalAutoHide = current;
        if (!Save(std::move(next))) return false;
    }
    if (current != preferences.autoHideTaskbar &&
        (error = port_.SetAutoHide(preferences.autoHideTaskbar)) != Error::None) return Fail(error);
    if (!ApplyVisibility(preferences.autoHideTaskbar && preferences.hideBehavior == HideBehavior::FullyHidden))
        return false;
    if (!preferences.transparentTaskbar) {
        if (record_.ownsCore) {
            error = port_.StopCore(!startedCore_);
            if (error != Error::None) return Fail(error);
            startedCore_ = false;
            auto next = record_; next.ownsCore = false;
            if (!Save(std::move(next))) return false;
        }
        while (!record_.appearances.empty()) {
            if (port_.RestoreAppearance(record_.appearances.back()) == RestoreResult::Failed)
                return Fail(Error::Recovery);
            auto next = record_; next.appearances.pop_back();
            if (!Save(std::move(next))) return false;
        }
    } else if (mode_ == Mode::Classic) {
        std::vector<Appearance> windows;
        error = port_.ReadClassic(windows);
        if (error != Error::None) return Fail(error);
        // Retire only targets whose HWND+PID+creation tuple has disappeared.
        auto next = record_;
        for (auto it = next.appearances.begin(); it != next.appearances.end();) {
            bool found = false;
            for (const auto& window : windows) if (SameTarget(*it, window)) { found = true; break; }
            if (!found) it = next.appearances.erase(it); else ++it;
        }
        for (const auto& window : windows) {
            bool found = false;
            for (const auto& original : next.appearances) if (SameTarget(original, window)) { found = true; break; }
            if (!found) next.appearances.push_back(window);
        }
        bool changed = next.appearances.size() != record_.appearances.size();
        if (!changed) for (std::size_t i = 0; i < next.appearances.size(); ++i)
            if (!SameTarget(next.appearances[i], record_.appearances[i])) { changed = true; break; }
        if (changed && !Save(std::move(next))) return false;
        for (const auto& window : record_.appearances)
            if ((error = port_.MakeTransparent(window)) != Error::None) return Fail(error);
    } else {
        if (startedCore_ && !port_.CoreAlive()) {
            startedCore_ = false;
            error = port_.StopCore(true);
            if (error != Error::None) return Fail(error);
        }
        if (!record_.ownsCore) {
            auto next = record_; next.ownsCore = true;
            if (!Save(std::move(next))) return false;
        }
        if (!startedCore_) {
            error = port_.StartCore();
            if (error != Error::None) return Fail(error);
            startedCore_ = true;
        }
    }
    error_ = Error::None; running_ = true; return true;
}
bool Controller::Shutdown(std::optional<Preferences> pending) {
    if (loaded_ == LoadState::Failed) return Fail(Error::Load);
    // Accepted UI edits must survive exit even when their asynchronous apply
    // was cancelled. Take only the ownership lock: do not stage transparency,
    // start a core, or apply effects while shutting down.
    bool saved = true;
    Error saveError = Error::None;
    if (pending && !(record_.preferences == *pending)) {
        saved = (acquired_ || Acquire(false));
        if (saved) { auto next = record_; next.preferences = *pending; saved = Save(std::move(next)); }
        if (!saved) saveError = error_;
    }
    if (!RestoreOwned()) return false;
    if (!saved) return Fail(saveError);
    initialRecovery_ = false; error_ = Error::None;
    return true;
}
Status Controller::Snapshot() const {
    return {record_.preferences, mode_, error_, running_, !running_ && HasRecovery(record_), record_.appearances.size(),
            Effective().enabled};
}
}  // namespace lume::taskbar
