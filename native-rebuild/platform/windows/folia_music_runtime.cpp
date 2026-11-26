#include "platform/windows/folia_music_runtime.h"
#include "platform/windows/folia_runtime_download.h"
#include "platform/windows/music_provider_credentials.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace lume::platform::windows {
namespace {
using music::auth::Json;
using music::auth::Provider;
constexpr std::size_t FrameLimit = 8 * 1024 * 1024;
std::filesystem::path ExecutableDirectory() {
    wchar_t value[32768]{};
    const auto size = GetModuleFileNameW(nullptr, value, 32768);
    if (!size || size >= 32768) throw std::runtime_error("unavailable");
    return std::filesystem::path(value).parent_path();
}
std::string PublicError(const Json& error) {
    const auto code = error.is_string() ? error.get<std::string>() : error.is_object() ? error.value("code", std::string{}) : std::string{};
    for (const auto* known : {"network", "timeout", "service", "unavailable", "unsupported", "invalid-response", "storage",
             "expired", "signed-out", "not-found", "not-playable", "preview-only", "rate-limited", "access-denied", "cancelled"})
        if (code == known) return code;
    return "service";
}
}
struct FoliaMusicRuntime::Impl {
    explicit Impl(std::filesystem::path value) : root(std::move(value)) {}
    ~Impl() { Shutdown(); }
    struct Pending { bool done = false; Json answer; };
    bool Alive() const { return alive.load(); }
    void Write(const Json& message, std::uint64_t expectedGeneration) {
        auto frame = message.dump() + "\n";
        if (frame.size() > FrameLimit) throw std::runtime_error("invalid-response");
        std::lock_guard lock(mutex);
        if (!Alive() || generation != expectedGeneration || outgoingBytes + frame.size() > FrameLimit * 2)
            throw std::runtime_error("unavailable");
        outgoingBytes += frame.size(); outgoing.push_back(std::move(frame));
        condition.notify_all();
    }
    void WriteLoop() {
        for (;;) {
            std::string frame;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return !Alive() || !outgoing.empty(); });
                if (!Alive()) return;
                frame = std::move(outgoing.front()); outgoing.pop_front(); outgoingBytes -= frame.size();
            }
            // Only this worker touches the pipe. UI cancellation only enqueues,
            // and a child that stops reading cannot hold shutdown or a caller.
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DWORD count = 0;
            bool success = operation.hEvent && WriteFile(input, frame.data(), static_cast<DWORD>(frame.size()), &count, &operation);
            if (!success && operation.hEvent && GetLastError() == ERROR_IO_PENDING) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (Alive() && std::chrono::steady_clock::now() < deadline &&
                       WaitForSingleObject(operation.hEvent, 20) == WAIT_TIMEOUT) {}
                success = GetOverlappedResult(input, &operation, &count, FALSE) != FALSE;
                if (!success) {
                    CancelIoEx(input, &operation);
                    GetOverlappedResult(input, &operation, &count, TRUE);
                }
            }
            if (success) success = GetOverlappedResult(input, &operation, &count, FALSE) != FALSE;
            if (operation.hEvent) CloseHandle(operation.hEvent);
            if (!success || count != frame.size()) {
                alive.store(false); condition.notify_all();
                if (job) TerminateJobObject(job, 97);
                return;
            }
        }
    }
    Json Transfer(std::string_view action, Provider provider, Json args, const music::auth::Cancelled& cancelled,
                  std::uint64_t expectedGeneration) {
        music::auth::CheckCancelled(cancelled);
        auto response = std::make_shared<Pending>();
        std::uint64_t id = 0;
        {
            std::lock_guard lock(mutex);
            if (closed || !Alive() || generation != expectedGeneration) throw std::runtime_error("unavailable");
            id = ++sequence; pending[id] = response;
        }
        try { Write({{"id", id}, {"action", action}, {"provider", music::auth::Name(provider)}, {"params", std::move(args)}}, expectedGeneration); }
        catch (...) { std::lock_guard lock(mutex); pending.erase(id); throw; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(action == "init" ? 60 : 45);
        std::unique_lock lock(mutex);
        while (!response->done && Alive() && !closed && generation == expectedGeneration) {
            if ((cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline) {
                pending.erase(id);
                lock.unlock();
                try { Write({{"id", 0}, {"action", "cancel-request"}, {"target", id}}, expectedGeneration); } catch (...) {}
                music::auth::CheckCancelled(cancelled);
                throw std::runtime_error("timeout");
            }
            condition.wait_for(lock, std::chrono::milliseconds(20));
        }
        pending.erase(id);
        if (!response->done || closed || generation != expectedGeneration) throw std::runtime_error("unavailable");
        auto answer = std::move(response->answer);
        lock.unlock();
        music::auth::CheckCancelled(cancelled);
        if (!answer.value("ok", false)) throw std::runtime_error(PublicError(answer.value("error", Json{})));
        return answer.value("result", Json{});
    }
    void ReadLoop() {
        std::string line;
        char buffer[8192]{};
        for (;;) {
            DWORD count = 0;
            if (!ReadFile(output, buffer, sizeof(buffer), &count, nullptr) || !count) break;
            for (DWORD i = 0; i < count; ++i) {
                if (buffer[i] != '\n') {
                    if (line.size() >= FrameLimit) { alive.store(false); condition.notify_all(); TerminateJobObject(job, 97); return; }
                    line += buffer[i]; continue;
                }
                auto message = Json::parse(line, nullptr, false); line.clear();
                if (!message.is_object()) continue;
                if (!message.contains("id") || !message["id"].is_number_unsigned()) continue;
                std::lock_guard lock(mutex);
                const auto found = pending.find(message["id"].get<std::uint64_t>());
                if (found == pending.end()) continue;
                found->second->answer = std::move(message); found->second->done = true;
                condition.notify_all();
            }
        }
        alive.store(false); condition.notify_all();
    }
    void Close() {
        // In-flight requests may still be leaving. They only access queued
        // state; process handles remain valid until both I/O threads finish.
        alive.store(false); condition.notify_all();
        if (job) TerminateJobObject(job, 0);
        if (writer.joinable()) writer.join();
        if (reader.joinable()) reader.join();
        if (input) { CloseHandle(input); input = nullptr; }
        if (output) { CloseHandle(output); output = nullptr; }
        if (process) { CloseHandle(process); process = nullptr; }
        if (job) { CloseHandle(job); job = nullptr; }
        { std::lock_guard lock(mutex); outgoing.clear(); outgoingBytes = 0; pending.clear(); sources.clear(); }
        initialized = false;
    }
    void Start(const music::auth::Cancelled& cancelled) {
        if (initialized && Alive()) return;
        Close();
        std::filesystem::path directory;
        try { directory = EnsureFoliaRuntime(ExecutableDirectory(), root, cancelled); }
        catch (...) { music::auth::CheckCancelled(cancelled); throw std::runtime_error("runtime-download"); }
        const auto executable = directory / L"LumeMusicProviderHost.exe";
        const auto script = directory / L"main.cjs";
        if (!std::filesystem::is_regular_file(executable) || !std::filesystem::is_regular_file(script) || root.empty())
            throw std::runtime_error("unavailable");
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE childInput = nullptr, childOutput = nullptr;
        static std::atomic<std::uint64_t> pipeSequence{0};
        const auto pipeName = L"\\\\.\\pipe\\LumeProvider." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++pipeSequence);
        input = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 0, 0, nullptr);
        if (input == INVALID_HANDLE_VALUE) input = nullptr;
        if (input) childInput = CreateFileW(pipeName.c_str(), GENERIC_READ, 0, &attributes, OPEN_EXISTING, 0, nullptr);
        if (childInput == INVALID_HANDLE_VALUE) childInput = nullptr;
        if (!input || !childInput || !CreatePipe(&output, &childOutput, &attributes, 0)) {
            if (childInput) CloseHandle(childInput);
            if (childOutput) CloseHandle(childOutput);
            Close(); throw std::runtime_error("unavailable");
        }
        SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
        HANDLE null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        bool valid = null != INVALID_HANDLE_VALUE && job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        bool allocated = valid && InitializeProcThreadAttributeList(list, 2, 0, &bytes);
        HANDLE inherited[]{childInput, childOutput, null};
        valid = allocated && UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) &&
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(job), nullptr, nullptr);
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = childInput; startup.StartupInfo.hStdOutput = childOutput; startup.StartupInfo.hStdError = null;
        startup.lpAttributeList = list;
        PROCESS_INFORMATION created{};
        auto command = L"\"" + executable.wstring() + L"\" --no-warnings \"" + script.wstring() + L"\"";
        const bool started = valid && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(), &startup.StartupInfo, &created);
        if (allocated) DeleteProcThreadAttributeList(list);
        CloseHandle(childInput); CloseHandle(childOutput);
        if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
        if (!started) { Close(); throw std::runtime_error("unavailable"); }
        process = created.hProcess;
        const bool resumed = ResumeThread(created.hThread) != static_cast<DWORD>(-1); CloseHandle(created.hThread);
        if (!resumed) { Close(); throw std::runtime_error("unavailable"); }
        { std::lock_guard lock(mutex); ++generation; }
        alive.store(true);
        reader = std::thread([this] { ReadLoop(); });
        writer = std::thread([this] { WriteLoop(); });
        Json legacy = Json::object();
        for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
            try { legacy[music::auth::Name(provider)] = music::auth::LoadCredentials(root, provider); }
            catch (...) { /* Keep an unreadable old file in place for recovery. */ }
        }
        Transfer("init", Provider::Netease, {{"root", root.u8string()}, {"legacy", std::move(legacy)}}, cancelled, generation);
        initialized = true;
    }
    Json Request(std::string_view action, Provider provider, Json args, const music::auth::Cancelled& cancelled) {
        std::uint64_t currentGeneration;
        {
            std::unique_lock operation(startMutex, std::defer_lock);
            while (!operation.try_lock_for(std::chrono::milliseconds(20))) {
                music::auth::CheckCancelled(cancelled);
                if (closed) throw std::runtime_error("unavailable");
            }
            if (closed) throw std::runtime_error("unavailable");
            Start(cancelled);
            currentGeneration = generation;
        }
        auto result = Transfer(action, provider, std::move(args), cancelled, currentGeneration);
        if (action == "online" && result.is_object() && result.contains("url") && result["url"].is_string()) {
            std::lock_guard lock(mutex);
            if (!Alive() || closed || generation != currentGeneration) throw std::runtime_error("unavailable");
            sources.insert(result["url"].get<std::string>());
            if (sources.size() > 128) sources.erase(sources.begin());
        }
        return result;
    }
    void Shutdown() {
        if (closed.exchange(true)) return;
        condition.notify_all();
        std::lock_guard operation(startMutex);
        try { if (Alive()) Write({{"id", 0}, {"action", "shutdown"}}, generation); } catch (...) {}
        if (process) WaitForSingleObject(process, 200);
        Close();
    }
    std::filesystem::path root;
    HANDLE process = nullptr, job = nullptr, input = nullptr, output = nullptr;
    std::thread reader, writer;
    mutable std::mutex mutex;
    std::timed_mutex startMutex;
    std::condition_variable condition;
    std::map<std::uint64_t, std::shared_ptr<Pending>> pending;
    std::set<std::string> sources;
    std::deque<std::string> outgoing;
    std::size_t outgoingBytes = 0;
    std::uint64_t sequence = 0, generation = 0;
    std::atomic<bool> alive{false}, closed{false};
    bool initialized = false;
};
FoliaMusicRuntime::FoliaMusicRuntime(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
FoliaMusicRuntime::~FoliaMusicRuntime() = default;
music::auth::Json FoliaMusicRuntime::Request(std::string_view action, Provider provider, Json arguments, const music::auth::Cancelled& cancelled) {
    return impl_->Request(action, provider, std::move(arguments), cancelled);
}
void FoliaMusicRuntime::CancelLogin(Provider provider, std::uint64_t attempt) {
    // No lazy start for cancellation. The worker's generation rejects late work.
    std::unique_lock operation(impl_->startMutex, std::try_to_lock);
    if (!operation.owns_lock() || !impl_->Alive()) return;
    try { impl_->Write({{"id", 0}, {"action", "auth-cancel"}, {"provider", music::auth::Name(provider)}, {"params", {{"attempt", attempt}}}}, impl_->generation); }
    catch (...) {}
}
void FoliaMusicRuntime::Shutdown() { impl_->Shutdown(); }
bool FoliaMusicRuntime::Available() const {
    try { return !impl_->closed && FoliaRuntimeAvailable(ExecutableDirectory(), impl_->root); }
    catch (...) { return false; }
}
void FoliaMusicRuntime::Prepare(const music::auth::Cancelled& cancelled,
    const std::function<void(std::string_view)>& phase) {
    const music::auth::Cancelled stop = [&] { return impl_->closed || (cancelled && cancelled()); };
    std::unique_lock lock(impl_->startMutex, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(20))) music::auth::CheckCancelled(stop);
    music::auth::CheckCancelled(stop);
    try { EnsureFoliaRuntime(ExecutableDirectory(), impl_->root, stop, phase); }
    catch (...) { music::auth::CheckCancelled(stop); throw std::runtime_error("runtime-download"); }
}
bool FoliaMusicRuntime::SourceAllowed(std::string_view value) const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->closed && impl_->Alive() && impl_->sources.count(std::string(value)) != 0;
}
std::shared_ptr<FoliaMusicRuntime> SharedFoliaMusicRuntime(const std::filesystem::path& root) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::weak_ptr<FoliaMusicRuntime>> instances;
    std::lock_guard lock(mutex);
    auto& entry = instances[root];
    auto value = entry.lock();
    if (!value) { value = std::make_shared<FoliaMusicRuntime>(root); entry = value; }
    return value;
}
}
