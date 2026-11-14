#include "apps/shell/diagnostic_job_session.h"
#include "platform/windows/lume_window_ids.h"
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace lume::shell {
namespace {
DiagnosticJobResult Execute(DiagnosticJob kind, const DiagnosticReportInput& input) {
    DiagnosticJobResult result;
    result.kind = kind;
    if (kind == DiagnosticJob::Report) result.report = WriteDiagnosticReport(input);
    else if (kind == DiagnosticJob::Copy) result.text = DiagnosticText(input);
    else result.checks = RunSelfChecks(input.installDirectory, input.modules);
    return result;
}
DiagnosticJobSession::Ports WindowsPorts() {
    return {
        Execute,
        [](DWORD thread) { PostThreadMessageW(thread, lume::window_ids::ShellWakeMessage, 0, 0); },
        [](std::function<void()> work) { return std::thread(std::move(work)); }
    };
}
}

struct DiagnosticJobSession::Impl {
    struct Shared {
        std::mutex mutex;
        std::optional<DiagnosticJobResult> result;
        std::atomic<DWORD> wakeThread{0};
        bool accepting = true; // guarded by mutex
    };
    explicit Impl(Ports value) : ports(std::move(value)) {}
    Ports ports;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::thread worker;
    bool running = false, stopped = false;

    void Shutdown() {
        if (stopped) return;
        stopped = true;
        shared->wakeThread.store(0);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->accepting = false;
            shared->result.reset();
        }
        // Workers never reference this object. Joining also finishes a wake
        // already in progress before the owner can be destroyed.
        if (worker.joinable()) worker.join();
        running = false;
    }
};

DiagnosticJobSession::DiagnosticJobSession() : DiagnosticJobSession(WindowsPorts()) {}
DiagnosticJobSession::DiagnosticJobSession(Ports ports) : impl_(std::make_unique<Impl>(std::move(ports))) {}
DiagnosticJobSession::~DiagnosticJobSession() { Shutdown(); }

DiagnosticJobSession::StartResult DiagnosticJobSession::Start(DiagnosticJob kind, DiagnosticReportInput input, DWORD wakeThread) {
    auto& state = *impl_;
    if (state.stopped) return StartResult::Stopped;
    if (state.running) return StartResult::Busy;
    if (state.worker.joinable()) state.worker.join();
    state.shared->wakeThread.store(wakeThread);
    state.running = true;
    try {
        if (!state.ports.execute || !state.ports.launch) throw std::runtime_error("missing diagnostic execution port");
        state.worker = state.ports.launch([
            shared = state.shared, execute = state.ports.execute, wake = state.ports.wake,
            kind, input = std::move(input)] {
            DiagnosticJobResult result;
            result.kind = kind;
            try {
                result = execute(kind, input);
                result.kind = kind;
            } catch (...) { result.report.error = "DIAGNOSTIC_JOB_FAILED"; }
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (!shared->accepting) return;
                shared->result = std::move(result);
            }
            if (const auto target = shared->wakeThread.load()) {
                try { if (wake) wake(target); } catch (...) {}
            }
        });
        if (!state.worker.joinable()) throw std::runtime_error("diagnostic launcher returned no worker");
        return StartResult::Started;
    } catch (...) {
        state.running = false;
        state.shared->wakeThread.store(0);
        std::lock_guard<std::mutex> lock(state.shared->mutex);
        state.shared->result.reset();
        return StartResult::ThreadFailed;
    }
}
bool DiagnosticJobSession::Running() const { return impl_->running; }
std::optional<DiagnosticJobResult> DiagnosticJobSession::Drain() {
    auto& state = *impl_;
    std::optional<DiagnosticJobResult> result;
    {
        std::lock_guard<std::mutex> lock(state.shared->mutex);
        result.swap(state.shared->result);
    }
    if (result) state.running = false;
    return result;
}
void DiagnosticJobSession::Shutdown() { impl_->Shutdown(); }
}
