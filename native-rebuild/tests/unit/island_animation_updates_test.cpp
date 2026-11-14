#include "platform/windows/dynamic_island/island_window.h"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
namespace {
constexpr UINT FrameMessage = WM_APP + 0x6e4;
unsigned checks = 0;
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
bool Equal(const WidgetBitmap& a, const WidgetBitmap& b) {
    return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}
int Px(HWND hwnd, float dip) {
    const auto canvasDpi = static_cast<unsigned>(std::lround(static_cast<float>(GetDpiForWindow(hwnd)) * 1.2f));
    return static_cast<int>(std::lround(dip * static_cast<float>(canvasDpi) / 96.f));
}
LPARAM RowPoint(HWND hwnd) { return MAKELPARAM(Px(hwnd, 60), Px(hwnd, 54)); }
// The desktop is never made the input desktop: fixture messages alone own hover.
unsigned Pump(unsigned milliseconds, HWND subject, HWND reference = nullptr) {
    unsigned frames = 0;
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if ((message.hwnd == subject || message.hwnd == reference) && message.message == WM_MOUSELEAVE) continue;
            if (message.hwnd == subject && message.message == FrameMessage) ++frames;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    } while (GetTickCount64() < end);
    return frames;
}
unsigned FinishNaturally(HWND subject, HWND reference) {
    unsigned frames = 0;
    auto lastFrame = GetTickCount64();
    const auto deadline = lastFrame + 6000;
    while (GetTickCount64() < deadline) {
        const auto count = Pump(10, subject, reference);
        frames += count;
        if (count) lastFrame = GetTickCount64();
        else if (GetTickCount64() - lastFrame >= 200) return frames;
    }
    throw std::runtime_error("animation did not stop posting frames naturally");
}
struct Inputs {
    std::vector<IslandTodo> rows;
    std::string edited;
    unsigned reads = 0;
    dynamic_island::Weather weather;
    IslandPresentation view;
    Inputs() {
        view.media.available = true;
        view.media.playing = false;
        view.media.id = "paused-probe";
        view.media.title = L"Before the animation update";
        view.media.artist = L"Lume";
        view.media.duration = 180;
        view.media.position = 45;
        view.time = L"10:08";
        view.date = L"Native animation state test";
        view.english = true;
        weather.enabled = weather.available = weather.hasRange = true;
        weather.city = L"Lume";
        weather.condition = L"Cloudy";
        weather.code = 3;
        Change(1);
    }
    void Change(unsigned revision) {
        const auto number = std::to_wstring(revision);
        view.media.title = L"Latest paused media " + number;
        weather.temperature = 10 + static_cast<int>(revision);
        weather.high = weather.temperature + 4;
        weather.low = weather.temperature - 3;
        rows = {{"latest-" + std::to_string(revision), L"Latest task " + number, L"10:08", false, false, 0x30d158, L""}};
    }
    void Bind(IslandWindow& window) {
        IslandTodoSource source;
        source.forDay = [this](calendar::Date) { ++reads; return rows; };
        source.hasTodos = [](calendar::Date) { return false; };
        source.writable = [] { return true; };
        source.edit = [this](const std::string& id, calendar::Date) { edited = id; };
        window.SetTodoSource(std::move(source));
    }
    void Apply(IslandWindow& window) const {
        window.Update(view);
        window.SetWeather(weather);
        window.RefreshTodos();
    }
};
void Compare(IslandWindow& subject, IslandWindow& reference, const char* reason) {
    const auto actual = subject.Frame();
    const auto expected = reference.Frame();
    if (!Equal(actual, expected)) {
        const auto common = std::min(actual.pixels.size(), expected.pixels.size());
        std::size_t first = common, different = 0;
        for (std::size_t i = 0; i < common; ++i) if (actual.pixels[i] != expected.pixels[i]) {
            if (first == common) first = i;
            ++different;
        }
        std::cerr << "Frame mismatch: actual=" << actual.width << 'x' << actual.height
                  << " expected=" << expected.width << 'x' << expected.height << " pixels=" << different;
        if (first != common)
            std::cerr << " first=" << first << " xy=" << first % actual.width << ',' << first / actual.width
                      << " BGRA=" << std::hex << actual.pixels[first] << '/' << expected.pixels[first] << std::dec;
        std::cerr << '\n';
    }
    Check(!actual.pixels.empty() && Equal(actual, expected), reason);
}
void ClickLatest(IslandWindow& window, Inputs& inputs) {
    inputs.edited.clear();
    SendMessageW(window.Handle(), WM_LBUTTONDOWN, 0, RowPoint(window.Handle()));
    SendMessageW(window.Handle(), WM_LBUTTONUP, 0, RowPoint(window.Handle()));
    Check(inputs.edited == inputs.rows.front().id, "final hit geometry edits the latest task");
}
void SuppressFallback(HWND hwnd) {
    // A one-second tick must not repair a missed final paint behind the test.
    KillTimer(hwnd, 1);
}
void ReadyForHover(IslandWindow& subject, HWND reference, LONG finalHeight) {
    const auto deadline = GetTickCount64() + 2000;
    RECT client{};
    do {
        Check(GetClientRect(subject.Handle(), &client) != FALSE, "read actual animated window geometry");
        // Hit() intentionally ignores workspace controls until Expansion() >= .9.
        // Use the actual target geometry so narrow displays with two rows obey
        // that contract too; 95% leaves time for the remaining spring tail.
        if (static_cast<double>(client.bottom) >= static_cast<double>(finalHeight) * .95) return;
        Pump(2, subject.Handle(), reference);
    } while (GetTickCount64() < deadline);
    Check(false, "opening reached interactive todo geometry before finishing");
}
void ApplyDuringOpening(IslandWindow& subject, IslandWindow& reference, Inputs& inputs,
                        bool animations, unsigned revision) {
    reference.Expand(true);
    reference.Settle();
    RECT finalClient{};
    Check(GetClientRect(reference.Handle(), &finalClient) != FALSE && finalClient.bottom > 0,
          "read reference target height before sending synthetic hover");
    subject.Expand(true);
    ReadyForHover(subject, reference.Handle(), finalClient.bottom);
    // Hover first: its enter animation is allowed to draw the old snapshot.
    SendMessageW(subject.Handle(), WM_MOUSEMOVE, 0, RowPoint(subject.Handle()));
    inputs.Change(revision);
    const auto readsBefore = inputs.reads;
    inputs.Apply(subject);
    const bool deferred = inputs.reads == readsBefore;
    const auto remainingFrames = FinishNaturally(subject.Handle(), reference.Handle());
    if (animations) {
        Check(deferred, "media/weather/todo updates avoid synchronous redraw while opening");
        Check(remainingFrames != 0, "latest updates arrived before the last real animation frame");
    }
    inputs.Apply(reference);
    reference.Expand(true);
    reference.Settle();
    SendMessageW(reference.Handle(), WM_MOUSEMOVE, 0, RowPoint(reference.Handle()));
    reference.Settle();
    Compare(subject, reference, "natural final frame matches latest todo/weather state and hover pixels");
    ClickLatest(subject, inputs);
}
void Scenario(IslandShape shape, bool animations) {
    Inputs inputs;
    IslandWindow subject(GetModuleHandleW(nullptr), {}), reference(GetModuleHandleW(nullptr), {});
    inputs.Bind(subject);
    inputs.Bind(reference);
    IslandDisplay display;
    display.autoHide = false;
    display.hideFullscreen = false;
    display.topmost = true;
    display.shape = shape;
    display.animationSpeed = IslandAnimationSpeed::Slow;
    display.bounce = IslandBounce::Normal;
    display.expandedWidgets = {dynamic_island::ExpandedWidget::Todos, dynamic_island::ExpandedWidget::CalendarWeather};
    widget::Preferences preferences;
    preferences.enabled = true;
    Check(subject.Start(preferences, display) && reference.Start(preferences, display), "isolated native windows start");
    SuppressFallback(subject.Handle());
    SuppressFallback(reference.Handle());
    inputs.Apply(subject);
    inputs.Apply(reference);
    subject.Settle();
    reference.Settle();

    ApplyDuringOpening(subject, reference, inputs, animations, 2);

    inputs.Change(3);
    inputs.Apply(subject);
    inputs.Apply(reference);
    reference.Settle();
    Compare(subject, reference, "updates after animation immediately paint the latest content");
    ClickLatest(subject, inputs);

    subject.Expand(false);
    FinishNaturally(subject.Handle(), reference.Handle());
    reference.Expand(false);
    reference.Settle();
    Compare(subject, reference, "natural collapse retains the latest paused-media title");

    subject.Stop();
    reference.Stop();
    Check(!subject.Handle() && subject.Frame().pixels.empty(), "stop releases native window and frame storage");
    Check(subject.Start(preferences, display) && reference.Start(preferences, display), "same objects restart after releasing state");
    SuppressFallback(subject.Handle());
    SuppressFallback(reference.Handle());
    inputs.Apply(subject);
    inputs.Apply(reference);
    subject.Settle();
    reference.Settle();
    ApplyDuringOpening(subject, reference, inputs, animations, 4);
    subject.Stop();
    reference.Stop();
}
void Run() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM on fixture desktop");
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    Check(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "initialize fixture graphics");
    BOOL animations = TRUE;
    Check(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) != FALSE, "read existing animation preference");
    try {
        Scenario(IslandShape::Capsule, animations != FALSE);
        Scenario(IslandShape::Standard, animations != FALSE);
    } catch (...) {
        Gdiplus::GdiplusShutdown(token);
        CoUninitialize();
        throw;
    }
    BOOL unchanged = TRUE;
    Check(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &unchanged, 0) != FALSE && unchanged == animations,
          "system animation preference is unchanged");
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    std::cout << "PASS island latest animation state: " << checks << " checks, system animations="
              << (animations != FALSE) << '\n';
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name = L"LumeIslandAnimationUpdates-" + std::to_wstring(GetCurrentProcessId());
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) { std::cerr << "FAIL: create isolated desktop\n"; return EXIT_FAILURE; }
    std::string failure;
    std::thread runner([&] {
        try {
            Check(SetThreadDesktop(desktop) != FALSE, "bind fixture thread to isolated desktop");
            Run();
        } catch (const std::exception& error) { failure = error.what(); }
    });
    runner.join();
    if (!CloseDesktop(desktop) && failure.empty()) failure = "close fixture desktop";
    if (!failure.empty()) { std::cerr << "FAIL: " << failure << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
