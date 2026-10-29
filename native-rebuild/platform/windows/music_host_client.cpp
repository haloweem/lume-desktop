#include "platform/windows/music_host_client.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <utility>
#include <vector>

namespace lume::music {
namespace {

HANDLE AsHandle(void* value) {
    return static_cast<HANDLE>(value);
}

void* AsPointer(HANDLE value) {
    return static_cast<void*>(value);
}

std::wstring PipeName() {
    return L"\\\\.\\pipe\\LumeMusic." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64());
}

bool WaitOverlapped(HANDLE handle, OVERLAPPED& overlapped,
                    std::chrono::steady_clock::time_point deadline,
                    const std::function<bool()>& cancelled = {}) {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if ((cancelled && cancelled()) || now >= deadline) break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const auto result = WaitForSingleObject(overlapped.hEvent,
            static_cast<DWORD>(std::clamp<std::int64_t>(remaining, 1, 25)));
        if (result == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            return GetOverlappedResult(handle, &overlapped, &transferred, FALSE) != FALSE;
        }
        if (result != WAIT_TIMEOUT) break;
    }
    CancelIoEx(handle, &overlapped);
    // Local named-pipe cancellation must complete before stack OVERLAPPED and
    // its buffer are released. The remote decoder never owns this I/O.
    DWORD transferred = 0;
    GetOverlappedResult(handle, &overlapped, &transferred, TRUE);
    return false;
}

}  // namespace

MusicHostClient::MusicHostClient(MusicHostClientOptions options) : options_(std::move(options)) {}

MusicHostClient::~MusicHostClient() {
    Stop();
}

bool MusicHostClient::Connect() {
    const HANDLE pipe = AsHandle(pipe_);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;
    const BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    bool result = connected != FALSE;
    const auto error = result ? ERROR_SUCCESS : GetLastError();
    if (!result && error == ERROR_PIPE_CONNECTED) result = true;
    else if (!result && error == ERROR_IO_PENDING) result = WaitOverlapped(pipe, overlapped,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.timeoutMs));
    CloseHandle(overlapped.hEvent);
    return result;
}

bool MusicHostClient::Start(EventHandler handler) {
    if (Running() || options_.executable.empty() ||
        GetFileAttributesW(options_.executable.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    const auto name = PipeName();
    const HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, static_cast<DWORD>(kMaxMusicHostFrameBytes),
        static_cast<DWORD>(kMaxMusicHostFrameBytes), options_.timeoutMs, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    pipe_ = AsPointer(pipe);

    std::wstring command = L"\"" + options_.executable + L"\" --pipe \"" + name + L"\"";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &processInfo)) {
        Stop();
        return false;
    }
    CloseHandle(processInfo.hThread);
    process_ = AsPointer(processInfo.hProcess);
    if (!Connect()) {
        Stop();
        return false;
    }
    handler_ = std::move(handler);
    return true;
}

bool MusicHostClient::Transfer(bool write, void* buffer, std::uint32_t bytes,
                               std::chrono::steady_clock::time_point deadline,
                               const std::function<bool()>& cancelled) {
    if (!Running() || !bytes) return bytes == 0;
    HANDLE pipe = AsHandle(pipe_);
    auto* cursor = static_cast<std::uint8_t*>(buffer);
    while (bytes > 0) {
        if ((cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline) return false;
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) return false;
        DWORD completed = 0;
        const BOOL ok = write
            ? WriteFile(pipe, cursor, bytes, &completed, &overlapped)
            : ReadFile(pipe, cursor, bytes, &completed, &overlapped);
        bool success = ok != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) success = WaitOverlapped(pipe, overlapped, deadline, cancelled);
        if (success && !completed) success = GetOverlappedResult(pipe, &overlapped, &completed, FALSE) != FALSE;
        CloseHandle(overlapped.hEvent);
        if (!success || !completed) return false;
        cursor += completed;
        bytes -= completed;
    }
    return true;
}

bool MusicHostClient::Send(std::string frame) {
    return Send(std::move(frame), {});
}

bool MusicHostClient::Send(std::string frame, const std::function<bool()>& cancelled) {
    if (!Running() || frame.empty() || frame.size() > kMaxMusicHostFrameBytes) return false;
    const auto command = DecodeHostCommand(frame);
    const auto timeout = command && command->action == "load" ? options_.loadTimeoutMs : options_.timeoutMs;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    const auto size = static_cast<std::uint32_t>(frame.size());
    auto frameSize = size;
    if (!Transfer(true, &frameSize, sizeof(frameSize), deadline, cancelled) ||
        !Transfer(true, frame.data(), size, deadline, cancelled)) {
        Stop();
        return false;
    }
    std::uint32_t responseSize = 0;
    if (!Transfer(false, &responseSize, sizeof(responseSize), deadline, cancelled) || responseSize == 0 ||
        responseSize > kMaxMusicHostFrameBytes) {
        Stop();
        return false;
    }
    std::string response(responseSize, '\0');
    if (!Transfer(false, response.data(), responseSize, deadline, cancelled) ||
        (cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline) {
        Stop();
        return false;
    }
    if (handler_) handler_(std::move(response));
    return true;
}

void MusicHostClient::StopProcess() {
    if (pipe_ != reinterpret_cast<void*>(-1)) {
        CancelIoEx(AsHandle(pipe_), nullptr);
        CloseHandle(AsHandle(pipe_));
        pipe_ = reinterpret_cast<void*>(-1);
    }
    if (process_) {
        const HANDLE process = AsHandle(process_);
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 500);
        }
        CloseHandle(process);
        process_ = nullptr;
    }
}

void MusicHostClient::Stop() {
    StopProcess();
    handler_ = {};
}

bool MusicHostClient::Running() const noexcept {
    return pipe_ != reinterpret_cast<void*>(-1) && process_ != nullptr &&
        WaitForSingleObject(AsHandle(process_), 0) == WAIT_TIMEOUT;
}

}  // namespace lume::music
