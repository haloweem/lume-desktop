#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace lume::platform::windows {
// Posts `message` to `window` once per refresh of the display the window is on
// (that output's vblank) while running, so animations step in time with the
// screen instead of a 15.6 ms timer. DwmFlush is only the fallback: with mixed
// refresh rates it follows the fastest monitor, so a window on a 60 Hz screen
// next to a 180 Hz one stepped about three times per shown frame, unevenly. At most one frame message is outstanding; the window calls
// Consumed() when it handles it. Pause() is cheap; the thread idles on a
// condition variable until the next Start().
class FrameClock final {
public:
    FrameClock() = default;
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;
    ~FrameClock() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
            active_ = false;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void Start(HWND window, UINT message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            window_ = window;
            message_ = message;
            active_ = true;
            // A queued frame may belong to the previous target window. Start a
            // new generation and clear the latch so a destroyed target can
            // never leave the next target waiting forever.
            pending_ = false;
            ++generation_;
        }
        if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        wake_.notify_all();
    }
    void Pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        pending_ = false;
    }
    void Consumed() { pending_ = false; }
    bool Running() const { return active_; }
    // QueryPerformanceCounter value of the last vblank of the window's display
    // the clock waited for; 0 before the first one, or while only DwmFlush works.
    std::int64_t LastVBlank() const { return lastVBlank_; }
private:
    void Run() {
        for (;;) {
            HWND window = nullptr;
            UINT message = 0;
            std::uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || active_; });
                if (quit_) return;
                window = window_;
                message = message_;
                generation = generation_;
            }
            // Blocks until the window's display refreshes; falls back to the
            // next composition, then to ~120 Hz.
            const bool synced = vblank_.Wait(window);
            if (synced) { LARGE_INTEGER at{}; QueryPerformanceCounter(&at); lastVBlank_ = at.QuadPart; }
            else if (FAILED(DwmFlush())) Sleep(8);
            // Retargeting while DwmFlush blocks must not post to the old
            // (possibly destroyed) window. The next loop will read the new
            // target and generation.
            if (!active_ || generation_.load() != generation) continue;
            if (!pending_.exchange(true) && !PostMessageW(window, message, 0, 0)) pending_ = false;
        }
    }
    // The vblank of the output showing a window. Used only on the clock
    // thread; DXGI is loaded on demand so users need no extra link library.
    class VBlank final {
    public:
        VBlank() = default;
        VBlank(const VBlank&) = delete;
        VBlank& operator=(const VBlank&) = delete;
        ~VBlank() { Reset(); if (factory_) factory_->Release(); if (dxgi_) FreeLibrary(dxgi_); }
        bool Wait(HWND window) {
            const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
            if (!output_ || monitor != monitor_ || (factory_ && !factory_->IsCurrent())) Resolve(monitor);
            if (output_ && SUCCEEDED(output_->WaitForVBlank())) return true;
            Reset();  // a removed or changed display: resolve again next frame
            return false;
        }
    private:
        void Reset() { if (output_) { output_->Release(); output_ = nullptr; } monitor_ = nullptr; }
        void Resolve(HMONITOR monitor) {
            Reset();
            if (factory_ && !factory_->IsCurrent()) { factory_->Release(); factory_ = nullptr; }
            if (!factory_) {
                if (!dxgi_) dxgi_ = LoadLibraryW(L"dxgi.dll");
                using Create = HRESULT(WINAPI*)(REFIID, void**);
                const auto create = dxgi_ ? reinterpret_cast<Create>(
                    reinterpret_cast<void*>(GetProcAddress(dxgi_, "CreateDXGIFactory1"))) : nullptr;
                if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory_)))) {
                    factory_ = nullptr;
                    return;
                }
            }
            IDXGIAdapter1* adapter = nullptr;
            for (UINT a = 0; !output_ && SUCCEEDED(factory_->EnumAdapters1(a, &adapter)); ++a) {
                IDXGIOutput* candidate = nullptr;
                for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, &candidate)); ++o) {
                    DXGI_OUTPUT_DESC description{};
                    if (SUCCEEDED(candidate->GetDesc(&description)) && description.Monitor == monitor) {
                        output_ = candidate;
                        break;
                    }
                    candidate->Release();
                }
                adapter->Release();
            }
            if (output_) monitor_ = monitor;
        }
        HMODULE dxgi_ = nullptr;
        IDXGIFactory1* factory_ = nullptr;
        IDXGIOutput* output_ = nullptr;
        HMONITOR monitor_ = nullptr;
    };
    VBlank vblank_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    HWND window_ = nullptr;
    UINT message_ = 0;
    std::atomic<bool> active_{false}, pending_{false};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::int64_t> lastVBlank_{0};
    bool quit_ = false;
};
}  // namespace lume::platform::windows
