#pragma once
#include "apps/shell/diagnostic_report.h"
#include "apps/shell/self_check.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace lume::shell {
enum class DiagnosticJob { Report, Copy, Check, StartupCheck };
struct DiagnosticJobResult {
    DiagnosticJob kind = DiagnosticJob::Report;
    DiagnosticReportResult report;
    std::string text;
    std::vector<SelfCheck> checks;
};

// The UI thread captures input, starts one job and drains its result. Workers
// own only input and a shared result slot; no DesktopApplication state crosses
// this boundary. Shutdown retires wakeup/results before joining the worker.
class DiagnosticJobSession final {
public:
    struct Ports {
        std::function<DiagnosticJobResult(DiagnosticJob, const DiagnosticReportInput&)> execute;
        // A numeric UI thread id, never an owner/window pointer. Production
        // posts the shell wake message; injected functions must own their data.
        std::function<void(DWORD)> wake;
        std::function<std::thread(std::function<void()>)> launch;
    };
    enum class StartResult { Started, Busy, ThreadFailed, Stopped };

    DiagnosticJobSession();
    explicit DiagnosticJobSession(Ports);
    ~DiagnosticJobSession();
    DiagnosticJobSession(const DiagnosticJobSession&) = delete;
    DiagnosticJobSession& operator=(const DiagnosticJobSession&) = delete;

    // These methods belong to the owning UI thread. Completion remains busy
    // until Drain consumes it, so a second request cannot overwrite a result.
    StartResult Start(DiagnosticJob, DiagnosticReportInput, DWORD wakeThread);
    bool Running() const;
    std::optional<DiagnosticJobResult> Drain();
    // Final, idempotent: no new jobs/results are accepted after shutdown.
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
