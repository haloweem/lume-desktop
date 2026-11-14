#include "apps/shell/desktop_service_lifecycle.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool value, const char* why) {
    if (!value) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
}
int main() {
    using lume::shell::DesktopServiceLifecycle;
    {
        DesktopServiceLifecycle lifecycle;
        std::vector<std::string> calls;
        bool wallpaperWork = false, recoveryWork = false;
        lifecycle.Add({"wallpaper", 30, [&] { calls.push_back("wallpaper-start"); },
            [&] { calls.push_back("wallpaper-stop"); return true; }, [&] { return wallpaperWork; }});
        lifecycle.Add({"widgets", 40, [&] { calls.push_back("widgets-start"); },
            [&] { calls.push_back("widgets-stop"); return true; }, {}});
        lifecycle.Add({"producer", 10, {}, [&] { calls.push_back("producer-stop"); return true; }, {}});
        lifecycle.Add({"recovery", 40, {}, [&] { calls.push_back("recovery-stop"); return true; }, [&] { return recoveryWork; }});
        Check(!lifecycle.HasWork(), "idle services do not force the desktop message loop to remain");
        wallpaperWork = true; Check(lifecycle.HasWork(), "background work survives closing a management page");
        wallpaperWork = false; recoveryWork = true;
        Check(lifecycle.HasWork(), "pending shell recovery keeps its host alive");
        lifecycle.Restore(); lifecycle.Restore();
        Check(calls == std::vector<std::string>{"wallpaper-start", "widgets-start"},
            "startup remains registration order and is consumed once");
        Check(lifecycle.Shutdown() && lifecycle.Shutdown(), "successful shutdown is idempotent");
        Check(calls == std::vector<std::string>{"wallpaper-start", "widgets-start", "producer-stop", "wallpaper-stop", "widgets-stop", "recovery-stop"},
            "shutdown uses explicit producer-first order and stable equal-priority order");
        Check(!lifecycle.HasWork(), "stopped services cannot revive the message loop");
        lifecycle.Restore(); Check(calls.size() == 6, "shutdown cannot restart services");
    }
    {
        DesktopServiceLifecycle lifecycle;
        unsigned first = 0, failed = 0, last = 0;
        lifecycle.Add({"first", 1, {}, [&] { ++first; return true; }, {}});
        lifecycle.Add({"failed", 2, {}, [&] { return ++failed > 1; }, {}});
        lifecycle.Add({"last", 3, {}, [&] { ++last; return true; }, {}});
        Check(!lifecycle.Shutdown() && first == 1 && failed == 1 && last == 1,
            "one failure does not leave later services running");
        Check(lifecycle.Shutdown() && first == 1 && failed == 2 && last == 1,
            "retry only revisits failed service ownership");
    }
    {
        DesktopServiceLifecycle lifecycle;
        unsigned attempts = 0, after = 0;
        lifecycle.Add({"throws", -1, {}, [&]() -> bool {
            if (++attempts == 1) throw std::runtime_error("fixture"); return true;
        }, {}});
        lifecycle.Add({"after", 0, {}, [&] { ++after; return true; }, {}});
        Check(!lifecycle.Shutdown() && after == 1, "exceptions do not abort teardown of other owners");
        Check(lifecycle.Shutdown() && attempts == 2 && after == 1, "an exceptional stop remains retryable");
    }
    {
        DesktopServiceLifecycle lifecycle;
        unsigned stops = 0;
        lifecycle.Add({"reentrant", 0, {}, [&] { ++stops; Check(!lifecycle.Shutdown(), "nested teardown is rejected"); return true; }, {}});
        Check(lifecycle.Shutdown() && stops == 1, "callbacks cannot reenter their own stop");
    }
    {
        DesktopServiceLifecycle lifecycle;
        lifecycle.Add({"one", 0, {}, {}, {}});
        bool duplicate = false, frozen = false;
        try { lifecycle.Add({"one", 1, {}, {}, {}}); } catch (const std::invalid_argument&) { duplicate = true; }
        lifecycle.Restore();
        try { lifecycle.Add({"late", 1, {}, {}, {}}); } catch (const std::logic_error&) { frozen = true; }
        Check(duplicate && frozen, "service ownership cannot be duplicated or changed during execution");
    }
    std::cout << "PASS desktop service startup, work ownership, shutdown order and retry lifecycle\n";
}
