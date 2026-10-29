#pragma once
#include "core/performance/performance_metrics.h"
#include "platform/windows/widget_ui/widget_canvas.h"
namespace lume::platform::windows {
std::wstring PerformancePercent(std::optional<double>);
std::wstring PerformanceSpeed(std::optional<double>);
std::wstring PerformanceMemory(const performance::Snapshot&);
// alpha fades the whole view (the dynamic island's page transition).
void DrawPerformance(WidgetCanvas&,float width,float height,const std::optional<performance::Snapshot>&,bool failed,
                     float alpha=1.f);
}
