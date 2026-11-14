#include "platform/windows/dynamic_island/notification_receiver.h"
#include <windows.h>
#include <appmodel.h>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

using namespace lume::platform::windows;
namespace {
void CheckAt(bool condition, int line) {
    if (!condition) { std::cerr << "notification receiver invariant failed at line " << line << '\n'; throw std::runtime_error("notification receiver invariant failed"); }
}
#define Check(condition) CheckAt((condition), __LINE__)
void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
template<class Predicate> void Eventually(Predicate predicate) {
    const auto deadline = GetTickCount64() + 4000;
    while (!predicate()) { Check(GetTickCount64() < deadline); PumpMessages(); Sleep(10); }
}
constexpr UINT ReceiverWakeMessage = WM_APP + 0x390;
struct WakeProbe {
    HWND window = nullptr;
    unsigned count = 0;
    WPARAM cookie = 0;
    bool valid = true;
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<WakeProbe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<WakeProbe*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == ReceiverWakeMessage) {
            ++self->count; self->cookie = wparam; self->valid = self->valid && lparam == 0;
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    WakeProbe() {
        WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = Proc;
        type.lpszClassName = L"LumeNotificationWakeProbe";
        Check(RegisterClassW(&type) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, type.hInstance, this);
        Check(window != nullptr);
    }
    ~WakeProbe() { if (window) DestroyWindow(window); }
};
HWND FindWindowForProcess(DWORD processId, const wchar_t* className) {
    struct Search { DWORD processId; const wchar_t* className; HWND found = nullptr; } search{processId, className};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& context = *reinterpret_cast<Search*>(parameter);
        DWORD process = 0; GetWindowThreadProcessId(window, &process);
        wchar_t name[128]{};
        if (process == context.processId && GetClassNameW(window, name, 128) && std::wcscmp(name, context.className) == 0) {
            context.found = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
int FixtureMain() {
    WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = FixtureProc;
    type.lpszClassName = L"LumeNotificationAttentionFixture";
    if (!RegisterClassW(&type)) return 2;
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"",
                                       WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, nullptr, nullptr, type.hInstance, nullptr);
    if (!window) return 3;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
struct AttentionFixture {
    std::wstring directory, executable;
    PROCESS_INFORMATION process{};
    HWND window = nullptr;
    ~AttentionFixture() {
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 1000) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 5); WaitForSingleObject(process.hProcess, 1000); }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
        if (!executable.empty()) DeleteFileW(executable.c_str());
        if (!directory.empty()) RemoveDirectoryW(directory.c_str());
    }
    void Start(const wchar_t* name = L"QQ.exe") {
        wchar_t temporary[MAX_PATH]{}, source[32768]{};
        Check(GetTempPathW(MAX_PATH, temporary) != 0);
        Check(GetModuleFileNameW(nullptr, source, static_cast<DWORD>(std::size(source))) != 0);
        directory = std::wstring(temporary) + L"LumeAttentionTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE);
        executable = directory + L"\\" + name;
        Check(CopyFileW(source, executable.c_str(), TRUE) != FALSE);
        auto command = L"\"" + executable + L"\" --attention-fixture";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process) != FALSE);
        Eventually([&] { window = FindWindowForProcess(process.dwProcessId, L"LumeNotificationAttentionFixture"); return window != nullptr; });
    }
};
void SendAttention(HWND receiver, HWND source) {
    DWORD_PTR result = 0;
    Check(SendMessageTimeoutW(receiver, RegisterWindowMessageW(L"SHELLHOOK"), HSHELL_FLASH,
                              reinterpret_cast<LPARAM>(source), SMTO_ABORTIFHUNG, 1000, &result) != 0);
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--attention-fixture") == 0) return FixtureMain();
    Check(IsQQNotificationSource(L"QQ"));
    for (const auto* source : {L"", L"qq", L"QQMusic", L"Tencent.QQMusic", L"NotQQ", L"QQ.exe", L"QQ "})
        Check(!IsQQNotificationSource(source));
    Check(IsQQExecutablePath(L"C:\\Apps\\Tencent\\QQ.exe"));
    for (const auto* path : {L"", L"C:\\QQMusic.exe", L"C:\\NotQQ.exe", L"C:\\QQ.exe.bak", L"C:\\QQ.exe\\Other.exe"})
        Check(!IsQQExecutablePath(path));
    using A = QQNotificationAccess; using C = QQNotificationChannel;
    Check(SelectQQNotificationChannel(true, false, A::Allowed, true) == C::System);
    Check(SelectQQNotificationChannel(true, true, A::Allowed, true) == C::System);
    Check(SelectQQNotificationChannel(true, false, A::Denied, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, true, A::Denied, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, true, A::Denied, true) == C::Attention);
    Check(SelectQQNotificationChannel(true, false, A::Unspecified, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, false, A::Unavailable, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, true, A::Unspecified, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, true, A::Allowed, false) == C::Attention);
    Check(SelectQQNotificationChannel(true, true, A::Unavailable, false) == C::Attention);
    Check(SelectQQNotificationChannel(false, false, A::Allowed, true) == C::Disabled);
    {
        QQNotificationDeduplicator seen;
        seen.Reset(1000);
        Check(!seen.Accept(10, 999));  // Initial baseline does not replay history.
        Check(seen.CanAccept(10, 1000));
        Check(seen.CanAccept(10, 1000)); // Failed payload construction does not consume a key.
        Check(seen.Accept(10, 1000));  // An event during baseline loading survives.
        Check(!seen.Accept(10, 1000)); // Event then full snapshot is one delivery.
        Check(seen.Accept(11, 1001));  // Full snapshot then event also deduplicates.
        Check(!seen.Accept(11, 1001));
        Check(seen.Accept(10, 1002));  // Windows numeric IDs may be reused.
        Check(!seen.Accept(10, 1000)); // Removal never forgets the old delivery.
        seen.Reset(2000);             // Disable/re-enable establishes a new baseline.
        Check(!seen.Accept(10, 1002));
        Check(seen.Accept(10, 2000));
        seen.Reset(1000);
        for (std::uint32_t id = 0; id < QQNotificationDeduplicator::Capacity + 4; ++id)
            Check(seen.Accept(id, 1000 + id));
        Check(!seen.Accept(0, 1000));  // Evicted keys cannot be replayed by snapshots.
        Check(seen.Accept(9000, 9999));
    }
    Check(IsWeChatExecutablePath(L"C:\\Program Files\\Tencent\\WeChat\\WeChat.exe"));
    Check(IsWeChatExecutablePath(L"D:\\Apps\\Weixin\\WEIXIN.EXE"));
    for (const auto* path : {L"", L"C:\\WeChat.exe\\Other.exe", L"C:\\NotWeChat.exe", L"C:\\WeChat.exe.bak"})
        Check(!IsWeChatExecutablePath(path));
    const auto start = std::chrono::steady_clock::now();
    IslandNotificationReceiver receiver;
    const auto state = receiver.Snapshot();
    Check(state.qq == "disabled" && state.wechat == "disabled");
    Check(!state.qqRealtime);
    Check(!receiver.RequestAccess());
    Check(receiver.Drain().empty());
    receiver.Configure({false, true, true, true});
    Check(receiver.Snapshot().qq == "disabled");
    Check(receiver.Snapshot().wechat == "disabled");
    Check(!receiver.Snapshot().qqRealtime);
    Check(!receiver.RequestAccess());
    Check(!receiver.OpenApp("wechat"));
    Check(!receiver.OpenApp("qq"));
    UINT32 packageLength = 0;
    if (GetCurrentPackageFullName(&packageLength, nullptr) == APPMODEL_ERROR_NO_PACKAGE) {
        AttentionFixture fixture; fixture.Start();
        receiver.Configure({true, true, false, false});
        Check(!receiver.RequestAccess());
        Eventually([&] { const auto value = receiver.Snapshot().qq; return value != "access-required" && value != "requesting"; });
        Check(receiver.Snapshot().wechat == "disabled");
        if (receiver.Snapshot().qq == "attention") {
            const auto hook = FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver");
            Check(hook != nullptr);
            SendAttention(hook, hook);
            Check(receiver.Drain().empty());
            SendAttention(hook, fixture.window);
            const auto first = receiver.Drain();
            Check(first.size() == 1 && first[0].source == "qq" && first[0].title.empty() && first[0].body.empty());
            SendAttention(hook, fixture.window);
            Check(receiver.Drain().empty());
        } else Check(receiver.Snapshot().qq == "connected" || receiver.Snapshot().qq == "package-required");
        receiver.Configure({true, false, true, false});
        Eventually([&] { return receiver.Snapshot().wechat == "listening"; });
        Check(receiver.Snapshot().qq == "disabled");
        Check(!receiver.Snapshot().qqRealtime);
        const auto hook = FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver");
        Check(hook != nullptr);
        SendAttention(hook, fixture.window);
        Check(receiver.Drain().empty());
        receiver.Configure({});
        Eventually([&] { return FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver") == nullptr; });
        Check(receiver.Snapshot().qq == "disabled" && receiver.Snapshot().wechat == "disabled");
    }
    {
        // The application source is an isolated, hidden test child. No real
        // chat content, Windows permission prompt or user application is used.
        AttentionFixture fixture; fixture.Start(L"WeChat.exe");
        WakeProbe probe;
        receiver.SetWakeTarget(probe.window, ReceiverWakeMessage, 17);
        receiver.Configure({true, false, true, false});
        Eventually([&] { return receiver.Snapshot().wechat == "listening"; });
        auto hook = FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver");
        Check(hook != nullptr);
        SendAttention(hook, fixture.window);
        Eventually([&] { return probe.count == 1; });
        Check(probe.cookie == 17 && probe.valid);
        receiver.SetWakeTarget(probe.window, ReceiverWakeMessage, 17);
        SendAttention(hook, fixture.window);
        PumpMessages(); Check(probe.count == 1);
        const auto pending = receiver.Drain();
        Check(pending.size() == 1 && pending[0].source == "wechat" && pending[0].title.empty() && pending[0].body.empty());

        receiver.SetWakeTarget(nullptr, 0, 0);
        receiver.Configure({});
        receiver.Configure({true, false, true, false});
        Eventually([&] { return receiver.Snapshot().wechat == "listening"; });
        hook = FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver");
        Check(hook != nullptr);
        SendAttention(hook, fixture.window);
        PumpMessages(); Check(probe.count == 1);
        receiver.SetWakeTarget(probe.window, ReceiverWakeMessage, 29);
        Eventually([&] { return probe.count == 2; });
        Check(probe.cookie == 29 && probe.valid);
        Check(receiver.Drain().size() == 1);

        receiver.Configure({});
        receiver.SetWakeTarget(probe.window, ReceiverWakeMessage, 31);
        PumpMessages(); Check(probe.count == 2);
        receiver.SetWakeTarget(nullptr, 0, 0);
        const auto shutdown = std::chrono::steady_clock::now();
        receiver.Shutdown();
        Check(std::chrono::steady_clock::now() - shutdown < std::chrono::seconds(2));
        receiver.SetWakeTarget(probe.window, ReceiverWakeMessage, 41);
        receiver.Configure({true, false, true, false});
        PumpMessages(); Check(probe.count == 2);
        Check(FindWindowForProcess(GetCurrentProcessId(), L"LumeIslandNotificationReceiver") == nullptr);
    }
    receiver.Shutdown();
    Check(receiver.Drain().empty());
    Check(!receiver.RequestAccess());
    Check(!receiver.Snapshot().qqRealtime);
    Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(9));
}
