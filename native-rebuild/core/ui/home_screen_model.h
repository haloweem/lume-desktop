#pragma once

namespace lume::ui {

enum class HomePage { Home, DesktopBeauty, ClockSettings, WallpaperPlaceholder };
enum class ClockStyle { Welfare, Mond, InsideDream };

struct HomeScreenSnapshot {
    HomePage page = HomePage::Home;
    bool clockEnabled = true;
    ClockStyle clockStyle = ClockStyle::Welfare;
    bool dirty = false;
};

class HomeScreenModel final {
public:
    HomeScreenModel() = default;
    explicit HomeScreenModel(bool clockEnabled, ClockStyle clockStyle);

    const HomeScreenSnapshot& Snapshot() const noexcept { return state_; }
    void Navigate(HomePage page) noexcept;
    void SetClockEnabled(bool enabled) noexcept;
    void SetClockStyle(ClockStyle style) noexcept;
    void LoadClockSettings(bool enabled, ClockStyle style) noexcept;
    void MarkSaved() noexcept { state_.dirty = false; }

private:
    HomeScreenSnapshot state_{};
};

}  // namespace lume::ui
