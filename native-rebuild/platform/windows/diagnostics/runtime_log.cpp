#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <eh.h>
#include <exception>
#include <thread>
#include <cstdio>
#include <mutex>
#include <system_error>
#include <vector>

namespace lume::platform::windows::diag {
namespace {
constexpr ULONGLONG MaxLogBytes = 2ull << 20;
constexpr std::size_t MaxLineBytes = 2048;
constexpr std::size_t KeptDumps = 3;

std::mutex& Lock() { static std::mutex lock; return lock; }
std::filesystem::path& Root() { static std::filesystem::path root; return root; }

std::string Stamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char text[64]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] ", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId());
    return text;
}

void AppendLocked(const std::string& line) {
    const auto& root = Root();
    if (root.empty()) return;
    const auto file = root / L"lume.log";
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) &&
        ((static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) >= MaxLogBytes)
        MoveFileExW(file.c_str(), (root / L"lume.1.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    const auto handle = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(handle);
}

std::wstring DumpName(const wchar_t* kind) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96]{};
    swprintf_s(name, L"LumeShell-%ls-%04u%02u%02u-%02u%02u%02u-%lu.dmp", kind, now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond, GetCurrentProcessId());
    return name;
}

void PruneDumps(const std::filesystem::path& folder) {
    std::error_code error;
    std::vector<std::filesystem::directory_entry> dumps;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        if (entry.path().extension() == L".dmp") dumps.push_back(entry);
    if (dumps.size() <= KeptDumps) return;
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) {
        std::error_code ignored;
        return a.last_write_time(ignored) > b.last_write_time(ignored);
    });
    for (std::size_t i = KeptDumps; i < dumps.size(); ++i) std::filesystem::remove(dumps[i].path(), error);
}

// The crash may have happened while the lock was held: never wait for it here.
void CrashLog(const std::string& message) {
    std::string line = Stamp() + "crash: " + message + "\r\n";
    std::unique_lock<std::mutex> lock(Lock(), std::try_to_lock);
    AppendLocked(line);
}

// Every thread's stack, plus the faulting context when there is one. Bounded:
// no full memory, newest KeptDumps files kept.
void WriteDump(EXCEPTION_POINTERS* exception, const wchar_t* kind) {
    const auto root = Root();
    if (root.empty()) return;
    const auto folder = root / L"crashes";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const auto path = folder / DumpName(kind);
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpScanMemory | MiniDumpWithUnloadedModules);
        const bool written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                               exception ? &info : nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(file);
        if (!written) DeleteFileW(path.c_str());
        else CrashLog("minidump written: " + path.filename().u8string());
    }
    PruneDumps(folder);
}

// Exits that skip the unhandled-exception filter: record them the same way.
[[noreturn]] void FatalExit(const char* reason) {
    static volatile LONG entered = 0;
    if (!InterlockedExchange(&entered, 1)) {
        CrashLog(std::string("LumeShell aborted: ") + reason);
        WriteDump(nullptr, L"abort");
    }
    TerminateProcess(GetCurrentProcess(), 3);
    std::abort();
}
void OnTerminate() { FatalExit("std::terminate (unhandled C++ exception)"); }
void OnPureCall() { FatalExit("pure virtual call"); }
void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    FatalExit("invalid CRT parameter");
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exception) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;
    const auto code = exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionCode : 0;
    const auto address = exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionAddress : nullptr;
    HMODULE module = nullptr;
    wchar_t moduleName[MAX_PATH]{};
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(address), &module))
        GetModuleFileNameW(module, moduleName, MAX_PATH);
    char line[640]{};
    std::snprintf(line, sizeof(line), "LumeShell crashed: %s at %p in %ls (+0x%llx)", ExitCodeText(code).c_str(), address,
                  moduleName[0] ? std::filesystem::path(moduleName).filename().c_str() : L"?",
                  module ? static_cast<unsigned long long>(static_cast<const char*>(address) - reinterpret_cast<const char*>(module)) : 0ull);
    CrashLog(line);
    WriteDump(exception, L"crash");
    // Windows Error Reporting still records the crash in the event log.
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void Initialize(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::lock_guard<std::mutex> lock(Lock());
    Root() = directory;
}

std::filesystem::path Directory() {
    std::lock_guard<std::mutex> lock(Lock());
    return Root();
}

void Log(std::string_view module, std::string_view message) {
    try {
        std::string line = Stamp();
        line.append(module.substr(0, 24));
        line.append(": ");
        line.append(message.substr(0, MaxLineBytes));
        for (auto& c : line) if (c == '\r' || c == '\n') c = ' ';
        line.append("\r\n");
        std::lock_guard<std::mutex> lock(Lock());
        AppendLocked(line);
    } catch (...) {}
}

void InstallCrashHandler() {
    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(OnTerminate);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
}

void StartHangWatch(HWND window) {
    static std::atomic_bool started{false};
    if (!window || started.exchange(true)) return;
    try {
        std::thread([window] {
            constexpr DWORD ProbeMs = 5000, HangMs = 15000;
            int dumps = 0;
            bool hung = false;
            ULONGLONG since = 0;
            while (IsWindow(window)) {
                DWORD_PTR ignored = 0;
                const auto start = GetTickCount64();
                const bool answered = SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG,
                                                          ProbeMs, &ignored) != 0;
                if (answered) {
                    if (hung) Log("hang", "UI thread responsive again after " + std::to_string((GetTickCount64() - since) / 1000) + " s");
                    hung = false; since = 0;
                    Sleep(ProbeMs);
                    continue;
                }
                if (!IsWindow(window)) break;
                if (!since) since = start;
                if (!hung && GetTickCount64() - since >= HangMs) {
                    hung = true;
                    Log("hang", "UI thread not responding for " + std::to_string((GetTickCount64() - since) / 1000) + " s");
                    // The dump shows where the UI thread is stuck. Two per run at most.
                    if (dumps < 2) { ++dumps; WriteDump(nullptr, L"hang"); }
                }
                Sleep(1000);
            }
        }).detach();
    } catch (...) {}
}

std::string ExitCodeText(unsigned long code) {
    const char* meaning = nullptr;
    switch (code) {
    case 0: meaning = "normal"; break;
    case 0xC0000005: meaning = "access violation"; break;
    case 0xC0000017: meaning = "out of memory"; break;
    case 0xC00000FD: meaning = "stack overflow"; break;
    case 0xC0000374: meaning = "heap corruption"; break;
    case 0xC0000409: meaning = "fail fast / stack buffer overrun"; break;
    case 0xC000001D: meaning = "illegal instruction"; break;
    case 0xC0000135: meaning = "DLL not found"; break;
    case 0xC0000142: meaning = "DLL initialization failed"; break;
    case 0x40010004: meaning = "terminated by debugger"; break;
    case 0xC000013A: meaning = "Ctrl+C / closed"; break;
    case 0x80000003: meaning = "breakpoint"; break;
    case 0xE06D7363: meaning = "C++ exception"; break;
    case 1: meaning = "terminated"; break;
    default: break;
    }
    char text[96]{};
    if (meaning) std::snprintf(text, sizeof(text), "0x%08lX (%s)", code, meaning);
    else std::snprintf(text, sizeof(text), "0x%08lX", code);
    return text;
}
}  // namespace lume::platform::windows::diag
