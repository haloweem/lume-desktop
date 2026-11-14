#pragma once
#include "core/dynamic_island/compact_widgets.h"
#include "core/dynamic_island/island_model.h"
#include "core/performance/performance_metrics.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <optional>
#include <string>

namespace lume::platform::windows {
struct CompactData {
    SYSTEMTIME time{};
    std::optional<double> battery,disk;
    bool plugged=false,hasBattery=false,charging=false;
    unsigned files=0;
    ULONGLONG refreshed=0,diskAt=0;
    void Refresh();
};
struct CompactValue {
    std::wstring text=L"—",secondary;
    std::optional<double> progress,secondProgress;
    std::uint32_t color=0x8abfff;
    bool available=false;
};
CompactValue ReadCompact(dynamic_island::CompactWidget,const CompactData&,const dynamic_island::Weather&,
    const std::optional<performance::Snapshot>&,bool english=false);
void DrawCompact(WidgetCanvas&,dynamic_island::CompactWidget,WidgetRect,const CompactData&,
    const dynamic_island::Weather&,const std::optional<performance::Snapshot>&,bool english,float alpha,bool right=false);
}
