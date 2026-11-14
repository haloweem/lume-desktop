#include "apps/shell/material_capture_permission_session.h"
#include "apps/shell/material_capture_permission.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Session = lume::shell::MaterialCapturePermissionSession;
using C = lume::ui::PageCommand;
void Expect(bool value, const char* why) {
    if (!value) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
struct Fake {
    std::optional<int> access = lume::shell::MaterialCaptureAccessPromptRequired;
    int reads = 0, requests = 0, cancels = 0;
    bool fail = false, failRead = false, synchronous = false, completeOnCancel = false;
    std::vector<Session::Completion> completions;
    Session::Ports Ports() {
        return {
            [this] {
                ++reads;
                if (failRead) throw std::runtime_error("injected access failure");
                return access;
            },
            [this](Session::Completion completed) -> std::function<void()> {
                ++requests;
                if (fail) throw std::runtime_error("injected request failure");
                completions.push_back(completed);
                if (synchronous) completed(lume::shell::MaterialCaptureAccessAllowed);
                return [this, completed] { ++cancels; if (completeOnCancel) completed(-1); };
            }
        };
    }
};
HWND Source() {
    const auto window = CreateWindowExW(0, L"STATIC", L"consent-test", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    Expect(window != nullptr, "create hidden command source");
    return window;
}
}

int main() {
    const auto source = Source(), other = Source();
    const auto thread = GetCurrentThreadId();
    {
        Session production;
        Expect(!production.Defer(C::DockLiquidGlass, source, thread, false), "production material choices never launch consent");
        Expect(!production.Drain(), "production has no deferred permission result");
    }
    {
        Fake fake; Session session(fake.Ports());
        Expect(!session.Defer(C::DockLiquidGlass, source, thread, true), "verification never prompts");
        Expect(!session.Defer(C::DockLiquidGlass, nullptr, thread, false), "no UI never prompts");
        Expect(!session.Defer(C::DockLiquidGlass, source, 0, false), "no live dispatcher never prompts");
        Expect(!session.Defer(C::DockEnable, source, thread, false), "enabling does not request capture consent");
        Expect(fake.reads == 0 && fake.requests == 0, "non-user choices do not touch the permission API");
        for (const auto access : {std::optional<int>{}, std::optional<int>{2}, std::optional<int>{4}}) {
            fake.access = access;
            Expect(!session.Defer(C::DockLiquidGlass, source, thread, false), "unsupported, denied and allowed pass through");
        }
        Expect(fake.requests == 0, "only prompt-required status starts a request");
    }
    {
        Fake fake; Session session(fake.Ports());
        Expect(session.Defer(C::DockLiquidGlass, source, thread, false), "right-click source can defer without manager");
        Expect(!session.Drain(), "uncompleted operation is not applied");
        Expect(!session.Defer(C::Ready, source, thread, false), "unrelated commands are not captured");
        Expect(session.Defer(C::IslandMaterialFrosted, other, thread, false, other), "later material replaces pending choice");
        Expect(fake.requests == 1, "replacement does not ask twice");
        fake.completions.front()(4);
        const auto result = session.Drain();
        Expect(result && result->command == C::IslandMaterialFrosted && result->source == other && result->target == other && result->access == 4,
            "completion retains latest command and its own source");
        Expect(!session.Drain(), "completed choice is consumed once");
        Expect(fake.cancels == 0, "draining a completed request does not cancel it");
    }
    {
        Fake fake; Session session(fake.Ports());
        session.Defer(C::DockLiquidGlass, source, thread, false);
        Expect(session.Defer(C::DockNoMaterial, source, thread, false), "solid selection replaces deferred glass");
        fake.completions.front()(2);
        const auto result = session.Drain();
        Expect(result && result->command == C::DockNoMaterial && result->access == 2,
            "denial cannot restore superseded glass");
    }
    {
        Fake fake; Session session(fake.Ports());
        session.Defer(C::TodayTodoLiquidGlass, source, thread, false, source);
        session.Defer(C::TodayTodoSolid, other, thread, false);
        fake.completions.front()(4);
        const auto result = session.Drain();
        Expect(result && result->command == C::TodayTodoSolid && result->source == other && !result->target,
            "a page choice replaces native instance scope with its original global scope");
    }
    {
        Fake fake; Session session(fake.Ports());
        session.Defer(C::DockLiquidGlass, source, thread, false);
        const auto cancelled = fake.completions.back();
        fake.completeOnCancel = true;
        session.Cancel(); session.Cancel();
        Expect(fake.cancels == 1, "cancellation is idempotent");
        session.Defer(C::CalendarFrostedGlass, other, thread, false);
        cancelled(4);
        Expect(!session.Drain(), "late cancelled result cannot complete a new operation");
        fake.completions.back()(2);
        const auto result = session.Drain();
        Expect(result && result->command == C::CalendarFrostedGlass && result->access == 2,
            "new request owns only its own result");
    }
    {
        Fake fake;
        { Session session(fake.Ports()); session.Defer(C::DockLiquidGlass, source, thread, false); }
        Expect(fake.cancels == 1, "destruction cancels outstanding request");
        fake.completions.back()(4); // Must not access the destroyed session or shell.
    }
    {
        Fake fake; Session session(fake.Ports()); fake.failRead = true;
        Expect(!session.Defer(C::DockLiquidGlass, source, thread, false) && !session.Drain(),
            "access exception leaves no deferred command");
        fake.failRead = false; fake.fail = true;
        Expect(!session.Defer(C::DockLiquidGlass, source, thread, false) && !session.Drain(),
            "request exception leaves no deferred command");
        fake.fail = false; fake.synchronous = true;
        Expect(session.Defer(C::DockFrostedGlass, source, thread, false), "failed request can retry");
        const auto result = session.Drain();
        Expect(result && result->command == C::DockFrostedGlass, "synchronous completion is not lost");
    }
    {
        Fake fake; Session session(fake.Ports()); const auto removed = Source();
        session.Defer(C::TodayTodoLiquidGlass, removed, thread, false);
        DestroyWindow(removed); fake.completions.back()(4);
        Expect(!session.Drain(), "closed source cannot receive a delayed material choice");
        Expect(session.Defer(C::DockLiquidGlass, source, thread, false), "closed source releases pending ownership");
    }
    {
        // A second owner of the same HWND models a new source generation. An
        // old request must neither consume it nor remove its lifetime marker.
        Fake older, newer; Session first(older.Ports()), second(newer.Ports());
        first.Defer(C::DockLiquidGlass, source, thread, false);
        second.Defer(C::CalendarFrostedGlass, source, thread, false);
        older.completions.back()(4);
        Expect(!first.Drain(), "an obsolete source generation cannot receive a deferred choice");
        newer.completions.back()(4);
        const auto result = second.Drain();
        Expect(result && result->command == C::CalendarFrostedGlass,
            "retiring an obsolete source does not clear the current owner's generation");
    }
    {
        using lume::shell::NativeMaterialRequest;
        using lume::shell::NativeMaterialTarget;
        Fake fake; Session session(fake.Ports());
        NativeMaterialRequest note{NativeMaterialTarget::Note,true,"exact-note-id",source};
        Expect(session.Defer(note,thread,false), "a typed note selection owns its own consent request");
        fake.completions.back()(4);
        const auto result=session.Drain();
        Expect(result&&result->native&&result->command==C::Invalid&&result->native->id=="exact-note-id"&&
            result->native->target==NativeMaterialTarget::Note&&result->native->frosted&&result->native->source==source,
            "typed completion preserves exact target, id, material and source without aliasing page commands");
        session.Defer(note,thread,false);
        session.Defer(C::DockNoMaterial,other,thread,false);
        fake.completions.back()(4);
        const auto page=session.Drain();
        Expect(page&&!page->native&&page->command==C::DockNoMaterial,
            "a later page selection replaces an outstanding typed selection");
        session.Defer(C::DockLiquidGlass,other,thread,false);
        NativeMaterialRequest music{NativeMaterialTarget::MusicDesktop,false,"",source};
        session.Defer(music,thread,false);
        fake.completions.back()(2);
        const auto native=session.Drain();
        Expect(native&&native->native&&native->native->target==NativeMaterialTarget::MusicDesktop&&native->access==2,
            "a typed selection replaces a page command and retains a denied result honestly");
        Expect(fake.requests==3, "cross-entry replacement does not create extra permission operations");
        session.Defer(note,thread,false);session.Cancel();fake.completions.back()(4);
        Expect(!session.Drain(), "cancelled typed choices cannot be replayed by a late callback");
    }
    DestroyWindow(source); DestroyWindow(other);
    std::cout << "PASS capture consent source, replacement, cancellation and late completion lifecycle\n";
}
