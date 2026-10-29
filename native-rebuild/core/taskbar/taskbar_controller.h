#pragma once
#include "core/taskbar/taskbar_store.h"
#include <optional>

namespace lume::taskbar {
enum class RestoreResult { Restored, Gone, Failed };
class ITaskbarPort {
public:
    virtual ~ITaskbarPort() = default;
    // Called only for enabled work or a durable pending recovery. Acquire must
    // validate capability/payload/conflicts before allowing any system mutation.
    virtual Error Acquire(bool transparency, Mode&) = 0;
    virtual void Release() = 0;
    virtual Error ReadAutoHide(bool&) = 0;
    virtual Error SetAutoHide(bool) = 0;
    virtual Error ReadVisibility(std::vector<Visibility>&) { return Error::Unsupported; }
    virtual Error Hide(const Visibility&) { return Error::Unsupported; }
    virtual RestoreResult RestoreVisibility(const Visibility&) { return RestoreResult::Failed; }
    // Shows taskbars left fully hidden without a journal entry.
    virtual void RepairHidden() {}
    virtual Error ReadClassic(std::vector<Appearance>&) = 0;
    virtual Error MakeTransparent(const Appearance&) = 0;
    virtual RestoreResult RestoreAppearance(const Appearance&) = 0;
    virtual Error StartCore() = 0;
    virtual bool CoreAlive() const = 0;
    // If recovery comes from a previous process, the Windows port restores via
    // the same verified core's normal shutdown path, and reports failure honestly.
    virtual Error StopCore(bool recoverPreviousProcess) = 0;
    virtual std::uintptr_t ExitSignal() const { return 0; }
};
class Controller final {
public:
    Controller(RecordStore&, std::string path, ITaskbarPort&);
    bool Apply(Preferences);
    // Another Lume surface may require the taskbar to stay
    // hidden. The demand is not persisted; the journal still restores the
    // user's original auto-hide state once neither owner needs it.
    void SetAutoHideDemand(bool value) { autoHideDemand_ = value; }
    bool AutoHideDemand() const { return autoHideDemand_; }
    bool Tick();
    bool Shutdown(std::optional<Preferences> pending = {});
    // While nothing should be fully hidden, show any taskbar still hidden
    // without a record. Call on the thread that owns the controller.
    void RepairOrphans();
    Status Snapshot() const;
    const Record& Journal() const { return record_; }
    LoadState Loaded() const { return loaded_; }
private:
    bool Save(Record);
    bool Acquire(bool transparency);
    bool RestoreOwned();
    bool ApplyVisibility(bool fullyHidden);
    bool Fail(Error);
    Preferences Effective() const;
    RecordStore& store_;
    std::string path_;
    ITaskbarPort& port_;
    Record record_;
    LoadState loaded_;
    Mode mode_ = Mode::Unknown;
    Error error_ = Error::None;
    bool acquired_ = false, transparencyAcquired_ = false;
    bool running_ = false, initialRecovery_ = false, startedCore_ = false;
    bool autoHideDemand_ = false;
    bool orphansRepaired_ = false;
};
}  // namespace lume::taskbar
