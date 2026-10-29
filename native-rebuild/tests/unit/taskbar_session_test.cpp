#include "apps/shell/taskbar_session.h"
#include "vendor/nlohmann/json.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace lume::taskbar;
namespace {
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }
class Port final : public ITaskbarPort {
public:
    bool hide = false, core = false;
    Error Acquire(bool, Mode& mode) override { mode = Mode::Windows11X64; return Error::None; }
    void Release() override {}
    Error ReadAutoHide(bool& value) override { value = hide; return Error::None; }
    Error SetAutoHide(bool value) override { hide = value; return Error::None; }
    Error ReadClassic(std::vector<Appearance>&) override { return Error::Unsupported; }
    Error MakeTransparent(const Appearance&) override { return Error::Unsupported; }
    RestoreResult RestoreAppearance(const Appearance&) override { return RestoreResult::Failed; }
    Error StartCore() override { core = true; return Error::None; }
    bool CoreAlive() const override { return core; }
    Error StopCore(bool) override { core = false; return Error::None; }
};
template<class Predicate> bool Pump(Predicate p) {
    const auto start = GetTickCount64();
    while (!p()) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (GetTickCount64() - start > 3000) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}
}
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        (L"lume-taskbar-session-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(root.is_absolute() && fs::equivalent(root.parent_path(), fs::temp_directory_path()) && fs::create_directory(root),
          "session test has isolated state directory");
    int callbacks = 0; const auto owner = GetCurrentThreadId(); bool error = false;
    {
        lume::shell::TaskbarSession session(GetModuleHandleW(nullptr), {(root / L"state.json").u8string(), {}, {}},
            [&](const std::wstring& text, bool failed) {
                Check(GetCurrentThreadId() == owner, "worker notification marshalled onto owning UI thread");
                const auto json = nlohmann::json::parse(text);
                Check(json.at("type") == "taskbar-state", "real session protocol");
                error = failed; ++callbacks;
            }, nullptr, [](const auto&, const auto&) { return std::make_unique<Port>(); });
        Check(session.Restore() && !session.Running(), "session starts disabled");
        auto p = session.Preferences();
        Check(!p.autoHideTaskbar && p.transparentTaskbar, "new session defaults to transparency without auto-hide");
        p.enabled = true; p.autoHideTaskbar = true;
        Check(session.Apply(p), "session accepts real preferences");
        Check(session.HasWork(), "queued enable keeps host alive before running");
        Check(Pump([&] { return callbacks && session.Running(); }) && !error, "completion reaches owner");
        p.transparentTaskbar = false;
        Check(session.Apply(p), "configure transparency independently");
        const auto before = callbacks;
        Check(Pump([&] { return callbacks > before; }), "configuration completion notification");
        Check(session.Shutdown() && !session.HasRecovery() && !session.HasWork(), "shutdown restores system ownership and permits host exit");
    }
    {
        lume::shell::TaskbarSession restored(GetModuleHandleW(nullptr), {(root / L"state.json").u8string(), {}, {}},
            {}, nullptr, [](const auto&, const auto&) { return std::make_unique<Port>(); });
        const auto p = restored.Preferences();
        Check(p.enabled && !p.transparentTaskbar && p.autoHideTaskbar, "all settings persisted across sessions");
        Check(restored.Restore() && Pump([&] { return restored.Running(); }), "persisted enabled component restarts");
        auto visible = restored.Preferences();
        visible.autoHideTaskbar = false;
        visible.hideBehavior = HideBehavior::FullyHidden;
        Check(restored.Apply(visible), "unchecked auto-hide preference accepted");
        Check(Pump([&] {
            const auto state = nlohmann::json::parse(restored.State());
            return !state.at("applying").get<bool>() &&
                   !restored.Preferences().autoHideTaskbar;
        }), "unchecked auto-hide preference committed before normal restart");
        Check(restored.Shutdown(), "second shutdown releases runtime");
    }
    {
        lume::shell::TaskbarSession restored(GetModuleHandleW(nullptr), {(root / L"state.json").u8string(), {}, {}},
            {}, nullptr, [](const auto&, const auto&) { return std::make_unique<Port>(); });
        const auto p = restored.Preferences();
        Check(p.enabled && !p.transparentTaskbar && !p.autoHideTaskbar &&
              p.hideBehavior == HideBehavior::FullyHidden,
              "unchecked options and remembered hide behavior survive a fresh session");
        Check(restored.Restore() && Pump([&] { return restored.Running(); }), "visible taskbar preference restarts");
        auto disabled = p; disabled.enabled = false;
        Check(restored.Apply(disabled), "last user edit accepted immediately before exit");
        Check(restored.Shutdown(), "immediate exit restores system ownership");
    }
    {
        lume::shell::TaskbarSession restored(GetModuleHandleW(nullptr), {(root / L"state.json").u8string(), {}, {}},
            {}, nullptr, [](const auto&, const auto&) { return std::make_unique<Port>(); });
        const auto p = restored.Preferences();
        Check(!p.enabled && !p.transparentTaskbar && !p.autoHideTaskbar &&
              p.hideBehavior == HideBehavior::FullyHidden,
              "final unchecked preference is durable even when exiting immediately");
        Check(restored.Shutdown(), "final session clean shutdown");
    }
    Check(fs::equivalent(root.parent_path(), fs::temp_directory_path()), "cleanup boundary");
    fs::remove_all(root);
    std::cout << "taskbar session passed\n";
}
