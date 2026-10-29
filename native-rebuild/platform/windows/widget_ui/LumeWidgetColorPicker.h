#ifndef LUME_WIDGET_COLOR_PICKER_H
#define LUME_WIDGET_COLOR_PICKER_H

#include <Windows.h>

struct LumeWidgetColor
{
	BYTE red;
	BYTE green;
	BYTE blue;
	BYTE alpha;
};

class LumeWidgetColorPicker
{
public:
	// The first swatch shows defaultColor, normally the setting's default.
	static bool Show(HWND owner, LumeWidgetColor& color, COLORREF defaultColor = RGB(44, 44, 46));
};

#endif
