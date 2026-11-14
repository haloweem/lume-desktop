#include "apps/shell/diagnostic_job_session.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace lume::shell;
using Start = DiagnosticJobSession::StartResult;
void Expect(bool value, const char* why) {
    if (!value) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
struct Signal {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned value = 0;
    void Set(unsigned next) {
        { std::lock_guard<std::mutex> lock(mutex); value = next; }
        changed.notify_all();
    }
    void Wait(unsigned minimum) {
        std::unique_lock<std::mutex> lock(mutex);
        Expect(changed.wait_for(lock, std::chrono::seconds(5), [&] { return value >= minimum; }),
            "bounded wait for worker progress");
    }
};
struct Gate {
    Signal entered, release;
    void Block() { entered.Set(1); release.Wait(1); }
};
DiagnosticReportInput Input() {
    DiagnosticReportInput input;
    input.note = "before"; input.version = "1.0.9";
    input.modules = {{"module", {{"enabled", true}}}};
    return input;
}
std::thread Launch(std::function<void()> work) { return std::thread(std::move(work)); }
}

int main() {
    {
        Gate gate; Signal woke;
        std::atomic<unsigned> launches{0}; std::atomic<DWORD> target{0};
        DiagnosticJobSession session({
            [&](DiagnosticJob kind, const DiagnosticReportInput& input) {
                gate.Block();
                DiagnosticJobResult result;
                result.kind = DiagnosticJob::StartupCheck; // The requested kind is authoritative.
                result.text = input.note + (input.modules["module"]["enabled"].get<bool>() ? ":on" : ":off");
                if (kind == DiagnosticJob::Check) result.checks.push_back({"fixture", CheckStatus::Ok, "", "", ""});
                return result;
            },
            [&](DWORD thread) { target.store(thread); woke.Set(launches.load()); },
            [&](std::function<void()> work) { ++launches; return Launch(std::move(work)); }
        });
        auto input = Input();
        Expect(session.Start(DiagnosticJob::Copy, input, 123) == Start::Started, "start a copied input snapshot");
        gate.entered.Wait(1);
        input.note = "after"; input.modules["module"]["enabled"] = false;
        Expect(session.Running() && !session.Drain(), "UI stays nonblocking while work is pending");
        Expect(session.Start(DiagnosticJob::Report, Input(), 999) == Start::Busy && launches == 1,
            "busy request does not replace the original input or wake target");
        gate.release.Set(1); woke.Wait(1);
        Expect(session.Running(), "completed but undrained result still owns the busy slot");
        Expect(session.Start(DiagnosticJob::Report, Input(), 999) == Start::Busy,
            "completed result cannot be overwritten before the UI consumes it");
        const auto result = session.Drain();
        Expect(result && result->kind == DiagnosticJob::Copy && result->text == "before:on" && target == 123,
            "worker uses a deep input snapshot and the original request identity");
        Expect(!session.Running() && !session.Drain(), "drain consumes a result exactly once");
        Expect(session.Start(DiagnosticJob::Check, Input(), 456) == Start::Started,
            "a drained session can launch another serialized task");
        woke.Wait(2);
        const auto checked = session.Drain();
        Expect(checked && checked->kind == DiagnosticJob::Check && checked->checks.size() == 1 && target == 456,
            "next job receives a fresh task kind and wake target");
    }
    {
        Signal woke; std::atomic<unsigned> count{0};
        DiagnosticJobSession session({
            [](DiagnosticJob kind, const DiagnosticReportInput&) {
                DiagnosticJobResult result; result.kind = kind;
                result.report.ok = true; result.report.path = L"fixture.zip";
                result.text = "fixture text";
                result.checks.push_back({"fixture", CheckStatus::Unknown, "", "", ""});
                return result;
            },
            [&](DWORD) { woke.Set(++count); }, Launch
        });
        unsigned expected = 0;
        for (const auto kind : {DiagnosticJob::Report, DiagnosticJob::Copy, DiagnosticJob::Check, DiagnosticJob::StartupCheck}) {
            Expect(session.Start(kind, Input(), 1) == Start::Started, "each existing diagnostic job can execute");
            woke.Wait(++expected);
            const auto result = session.Drain();
            Expect(result && result->kind == kind && result->report.ok && result->report.path == L"fixture.zip" &&
                result->text == "fixture text" && result->checks.size() == 1,
                "result payloads cross the worker boundary without presentation changes");
        }
    }
    {
        Signal woke; std::atomic<unsigned> count{0};
        DiagnosticJobSession session({
            [](DiagnosticJob, const DiagnosticReportInput&) -> DiagnosticJobResult { throw std::runtime_error("fixture"); },
            [&](DWORD) { woke.Set(++count); }, Launch
        });
        unsigned expected = 0;
        for (const auto kind : {DiagnosticJob::Report, DiagnosticJob::Copy, DiagnosticJob::Check, DiagnosticJob::StartupCheck}) {
            Expect(session.Start(kind, Input(), 1) == Start::Started, "exception fixture starts");
            woke.Wait(++expected);
            const auto result = session.Drain();
            Expect(result && result->kind == kind && result->report.error == "DIAGNOSTIC_JOB_FAILED" &&
                !result->report.ok && result->text.empty() && result->checks.empty(),
                "worker exceptions preserve the existing failure payload and requested job kind");
        }
    }
    {
        Signal woke; unsigned launches = 0;
        DiagnosticJobSession session({
            [](DiagnosticJob, const DiagnosticReportInput&) { return DiagnosticJobResult{}; },
            [&](DWORD) { woke.Set(1); },
            [&](std::function<void()> work) {
                if (++launches == 1) throw std::runtime_error("thread unavailable");
                return Launch(std::move(work));
            }
        });
        Expect(session.Start(DiagnosticJob::Report, Input(), 1) == Start::ThreadFailed,
            "thread creation failure returns its existing failure category");
        Expect(!session.Running() && !session.Drain(), "failed launch leaves no busy or phantom result");
        Expect(session.Start(DiagnosticJob::Report, Input(), 2) == Start::Started, "a failed thread launch can retry");
        woke.Wait(1); Expect(session.Drain().has_value(), "retry owns its own result");
    }
    {
        Signal woke;
        DiagnosticJobSession session({
            [](DiagnosticJob, const DiagnosticReportInput&) { return DiagnosticJobResult{}; },
            [&](DWORD) { woke.Set(1); throw std::runtime_error("wake unavailable"); }, Launch
        });
        session.Start(DiagnosticJob::Report, Input(), 1); woke.Wait(1);
        Expect(session.Drain().has_value(), "a failed notification cannot discard a completed result");
    }
    {
        Signal woke;
        DiagnosticJobSession session({
            [](DiagnosticJob, const DiagnosticReportInput&) { return DiagnosticJobResult{}; },
            [&](DWORD) { woke.Set(1); }, Launch
        });
        session.Start(DiagnosticJob::Report, Input(), 1); woke.Wait(1);
        session.Shutdown(); session.Shutdown();
        Expect(!session.Running() && !session.Drain(), "shutdown discards undelivered completed results exactly once");
        Expect(session.Start(DiagnosticJob::Copy, Input(), 1) == Start::Stopped,
            "shutdown cannot accidentally restart diagnostics");
    }
    {
        Gate gate; std::atomic<bool> finished{false}; std::atomic<unsigned> wakes{0};
        DiagnosticJobSession session({
            [&](DiagnosticJob, const DiagnosticReportInput&) {
                gate.Block(); finished = true; return DiagnosticJobResult{};
            },
            [&](DWORD) { ++wakes; }, Launch
        });
        session.Start(DiagnosticJob::StartupCheck, Input(), 1); gate.entered.Wait(1);
        std::thread release([&] { gate.release.Set(1); });
        session.Shutdown(); release.join();
        Expect(finished && !session.Running() && !session.Drain(),
            "shutdown joins pending work before releasing its owned data");
        const auto finalWakes = wakes.load();
        session.Shutdown();
        Expect(wakes == finalWakes, "no worker notification remains after shutdown returns");
    }
    {
        Gate gate; std::atomic<bool> finished{false};
        std::thread release;
        {
            DiagnosticJobSession session({
                [&](DiagnosticJob, const DiagnosticReportInput&) {
                    gate.Block(); finished = true; return DiagnosticJobResult{};
                }, {}, Launch
            });
            session.Start(DiagnosticJob::Copy, Input(), 0); gate.entered.Wait(1);
            release = std::thread([&] { gate.release.Set(1); });
        }
        release.join();
        Expect(finished, "destruction owns worker joining without relying on host State member order");
    }
    {
        DiagnosticJobSession session;
        Expect(!session.Running() && !session.Drain(), "production adapter construction has no diagnostic side effects");
        session.Shutdown();
        Expect(session.Start(DiagnosticJob::Report, Input(), 1) == Start::Stopped,
            "default adapter honors the same stopped lifecycle without running external work");
    }
    std::cout << "PASS diagnostic job snapshot, serialization, failure, wake and shutdown lifecycle\n";
}
