#include "platform/windows/dynamic_island/player_position.h"
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>

namespace lume::platform::windows {
namespace {
template<class T> struct Com {
    T* value = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { Reset(); }
    void Reset() { if (value) value->Release(); value = nullptr; }
    T** Put() { Reset(); return &value; }
    T* operator->() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};
struct Bstr {
    BSTR value = nullptr;
    ~Bstr() { SysFreeString(value); }
    std::wstring Text() const { return value ? std::wstring(value, SysStringLen(value)) : std::wstring(); }
};
// "mm:ss" or "h:mm:ss", optionally padded with spaces; seconds or -1.
int Clock(const std::wstring& text) {
    int total = 0, part = 0, digits = 0, parts = 0;
    for (const wchar_t c : text) {
        if (c == L' ' || c == 0xa0) { if (digits || parts) return -1; continue; }
        if (c >= L'0' && c <= L'9') { part = part * 10 + (c - L'0'); if (++digits > 3) return -1; continue; }
        if (c != L':' || !digits) return -1;
        total = total * 60 + part; part = digits = 0;
        if (++parts > 2) return -1;
    }
    if (!parts || digits != 2) return -1;
    return total * 60 + part;
}
std::wstring Trim(std::wstring value) {
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    return value;
}
bool QQMusic(DWORD pid) {
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const bool named = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    const wchar_t* file = named ? wcsrchr(path, L'\\') : nullptr;
    return file && _wcsicmp(file + 1, L"QQMusic.exe") == 0;
}
}  // namespace

struct PlayerPosition::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false, done = false;
    ULONGLONG requested = 0;
    std::wstring title;  // wanted track (window title starts with it)
    // Latest reading.
    bool valid = false;
    int seconds = -1, total = 0;
    ULONGLONG changedAt = 0, sampledAt = 0;

    // Worker-only state.
    Com<IUIAutomation> uia;
    Com<IUIAutomationElement> elapsed, length;
    HWND window = nullptr;
    std::wstring windowTitle;

    HWND FindWindowFor(const std::wstring& wanted) {
        struct Search { std::wstring wanted; HWND found = nullptr; } search{wanted};
        EnumWindows([](HWND h, LPARAM p) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(p);
            wchar_t cls[64]{};
            if (!GetClassNameW(h, cls, 64) || wcscmp(cls, L"TXGuiFoundation") != 0) return TRUE;
            wchar_t text[512]{};
            GetWindowTextW(h, text, 512);
            if (s.wanted.empty() || wcsncmp(text, s.wanted.c_str(), s.wanted.size()) != 0) return TRUE;
            DWORD pid = 0;
            if (!GetWindowThreadProcessId(h, &pid) || !QQMusic(pid)) return TRUE;
            s.found = h;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }
    // The first two adjacent texts that both read as clock times: elapsed, total.
    bool Locate(HWND target) {
        elapsed.Reset(); length.Reset();
        Com<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(target, root.Put())) || !root) return false;
        VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_TextControlTypeId;
        Com<IUIAutomationCondition> texts;
        if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, texts.Put()))) return false;
        Com<IUIAutomationElementArray> all;
        if (FAILED(root->FindAll(TreeScope_Descendants, texts.value, all.Put())) || !all) return false;
        int count = 0;
        all->get_Length(&count);
        int previous = -1;
        Com<IUIAutomationElement> last;
        for (int i = 0; i < count && i < 400; ++i) {
            Com<IUIAutomationElement> item;
            if (FAILED(all->GetElement(i, item.Put())) || !item) continue;
            Bstr name;
            item->get_CurrentName(&name.value);
            const int value = Clock(name.Text());
            if (value >= 0 && previous >= 0 && value >= previous) {
                elapsed.value = last.value; last.value = nullptr;
                length.value = item.value; item.value = nullptr;
                return true;
            }
            previous = value;
            last.value = item.value; item.value = nullptr;
            if (value < 0) last.Reset();
        }
        return false;
    }
    void Sample(const std::wstring& wanted) {
        if (!window || !IsWindow(window) || wanted != windowTitle) {
            elapsed.Reset(); length.Reset();
            window = FindWindowFor(wanted);
            windowTitle = wanted;
            if (!window || !Locate(window)) { window = nullptr; std::lock_guard lock(mutex); valid = false; return; }
        }
        Bstr a, b;
        int now = -1, all = -1;
        if (elapsed && SUCCEEDED(elapsed->get_CurrentName(&a.value))) now = Clock(a.Text());
        if (length && SUCCEEDED(length->get_CurrentName(&b.value))) all = Clock(b.Text());
        const auto tick = GetTickCount64();
        std::lock_guard lock(mutex);
        if (now < 0) { valid = false; window = nullptr; return; }
        // A one-second step happened just now; anything else (first read,
        // seek, new track) leaves the fraction unknown: assume half a second.
        if (now != seconds) changedAt = valid && now == seconds + 1 ? tick : tick - 500;
        seconds = now;
        total = std::max(all, 0);
        sampledAt = tick;
        valid = true;
    }
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                reinterpret_cast<void**>(uia.Put())))) uia.Reset();
        for (;;) {
            std::wstring wanted;
            bool active = false;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(250), [this] { return stop; });
                if (stop) break;
                active = GetTickCount64() - requested < 3000;
                wanted = title;
                if (!active) valid = false;
            }
            if (!active || !uia) { elapsed.Reset(); length.Reset(); window = nullptr; continue; }
            try { Sample(wanted); } catch (...) { window = nullptr; }
        }
        elapsed.Reset(); length.Reset(); uia.Reset();
        CoUninitialize();
    }
};

PlayerPosition::PlayerPosition() : impl_(std::make_shared<Impl>()) {
    worker_ = std::thread([state = impl_] {
        state->Run();
        std::lock_guard lock(state->mutex);
        state->done = true;
        state->wake.notify_all();
    });
}
PlayerPosition::~PlayerPosition() {
    std::unique_lock lock(impl_->mutex);
    impl_->stop = true;
    impl_->wake.notify_all();
    // A UI Automation call into a hung player must not stall exit.
    const bool finished = impl_->wake.wait_for(lock, std::chrono::milliseconds(1500), [this] { return impl_->done; });
    lock.unlock();
    if (finished) worker_.join();
    else worker_.detach();
}
std::optional<PlayerPosition::Reading> PlayerPosition::Read(const std::string& app, const std::wstring& title, bool playing) {
    if (_stricmp(app.c_str(), "QQMusic.exe") != 0 || title.empty()) return {};
    const auto now = GetTickCount64();
    std::lock_guard lock(impl_->mutex);
    impl_->requested = now;
    if (impl_->title != title) { impl_->title = title; impl_->valid = false; impl_->seconds = -1; }
    if (!impl_->valid || now - impl_->sampledAt > 1500) return {};
    const double fraction = playing ? std::clamp(static_cast<double>(now - impl_->changedAt) / 1000, 0.0, .999) : 0;
    Reading reading{impl_->seconds + fraction, static_cast<double>(impl_->total)};
    if (reading.duration > 0) reading.position = std::min(reading.position, reading.duration);
    return reading;
}

}  // namespace lume::platform::windows
