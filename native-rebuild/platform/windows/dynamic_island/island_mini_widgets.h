#pragma once
#include "platform/windows/dynamic_island/island_window.h"

namespace lume::platform::windows {
struct IslandMiniData {
    SYSTEMTIME time{};
    std::optional<double> battery,disk;
    ULONGLONG diskAt=0;
    void Refresh(const dynamic_island::Customization&);
};
float IslandMiniWidth(const dynamic_island::MiniSelection&);
void DrawIslandMiniWidget(WidgetCanvas&,const dynamic_island::MiniSelection&,WidgetRect,
    const IslandMiniData&,const IslandPresentation&,const dynamic_island::Weather&,
    const std::optional<performance::Snapshot>&,float alpha);
}
