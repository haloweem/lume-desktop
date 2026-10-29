#pragma once
#include "core/dynamic_island/island_model.h"
#include "platform/windows/widget_ui/widget_canvas.h"

namespace lume::platform::windows {

// The island's weather page: today's overview above six hourly forecasts.
void DrawIslandWeatherPage(WidgetCanvas&, const dynamic_island::Weather&, float width, float height, float alpha);
// A coarse weather glyph for a WMO weather code.
void DrawIslandWeatherGlyph(WidgetCanvas&, float cx, float cy, float size, int code, bool day, float alpha);

}  // namespace lume::platform::windows
