#include "StdAfx.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "LumeWidgetColorPicker.h"
#include "platform/windows/rounded_window_surface.h"

#include <array>
#include <commctrl.h>

namespace
{
constexpr WCHAR kWindowClass[] = L"LumeWidgetColorPickerWindow";
constexpr int kLogicalWidth = 320;
constexpr int kLogicalHeight = 620;
constexpr int kEditHex = 5101;
constexpr int kEditRed = 5102;
constexpr int kEditGreen = 5103;
constexpr int kEditBlue = 5104;
constexpr int kEditAlpha = 5105;

constexpr COLORREF kPresetColors[] =
{
	RGB(44, 44, 46), RGB(0, 0, 0), RGB(255, 255, 255), RGB(234, 67, 53),
	RGB(255, 128, 0), RGB(255, 235, 20),
	RGB(183, 154, 115), RGB(103, 200, 73), RGB(129, 207, 211),
	RGB(30, 93, 145), RGB(162, 161, 170), RGB(157, 178, 215),
	RGB(132, 0, 255), RGB(255, 0, 214)
};

// The first swatch is the default of whatever is being coloured, so it can be picked again.
thread_local COLORREF firstPreset = RGB(44, 44, 46);
COLORREF Preset(int i) { return i == 0 ? firstPreset : kPresetColors[i]; }

struct PickerState
{
	HWND window = nullptr;
	HWND owner = nullptr;
	std::array<HWND, 5> edits = {};
	HFONT editFont = nullptr;
	LumeWidgetColor color = {};
	double hue = 0.0;
	double tone = 0.5;
	double scale = 1.0;
	int lastColorEdit = kEditHex;
	bool updatingEdits = false;
	bool accepted = false;
	bool done = false;
	bool draggingHue = false;
	bool draggingTone = false;
	bool draggingAlpha = false;
};

int Scale(const PickerState* state, int value)
{
	return (int)std::lround(value * state->scale);
}

BYTE ClampByte(int value)
{
	return (BYTE)min(255, max(0, value));
}

Gdiplus::Color ToGdiColor(const LumeWidgetColor& color, BYTE alpha = 255)
{
	return Gdiplus::Color(alpha, color.red, color.green, color.blue);
}

LumeWidgetColor HsvToRgb(double hue, double saturation, double value, BYTE alpha)
{
	hue -= floor(hue);
	const double chroma = value * saturation;
	const double sector = hue * 6.0;
	const double x = chroma * (1.0 - fabs(fmod(sector, 2.0) - 1.0));
	double red = 0.0;
	double green = 0.0;
	double blue = 0.0;

	switch ((int)floor(sector) % 6)
	{
	case 0: red = chroma; green = x; break;
	case 1: red = x; green = chroma; break;
	case 2: green = chroma; blue = x; break;
	case 3: green = x; blue = chroma; break;
	case 4: red = x; blue = chroma; break;
	default: red = chroma; blue = x; break;
	}

	const double match = value - chroma;
	return
	{
		(BYTE)std::lround((red + match) * 255.0),
		(BYTE)std::lround((green + match) * 255.0),
		(BYTE)std::lround((blue + match) * 255.0),
		alpha
	};
}

void SyncModelFromColor(PickerState* state)
{
	const double red = state->color.red / 255.0;
	const double green = state->color.green / 255.0;
	const double blue = state->color.blue / 255.0;
	const double maximum = max(red, max(green, blue));
	const double minimum = min(red, min(green, blue));
	const double delta = maximum - minimum;

	if (delta > 0.0001)
	{
		if (maximum == red)
		{
			state->hue = fmod((green - blue) / delta, 6.0) / 6.0;
		}
		else if (maximum == green)
		{
			state->hue = ((blue - red) / delta + 2.0) / 6.0;
		}
		else
		{
			state->hue = ((red - green) / delta + 4.0) / 6.0;
		}
		if (state->hue < 0.0) state->hue += 1.0;
	}

	const double saturation = maximum <= 0.0001 ? 0.0 : delta / maximum;
	state->tone = maximum > 0.995
		? saturation * 0.5
		: 1.0 - maximum * 0.5;
	state->tone = min(1.0, max(0.0, state->tone));
}

void SyncColorFromModel(PickerState* state)
{
	double saturation;
	double value;
	if (state->tone <= 0.5)
	{
		saturation = state->tone * 2.0;
		value = 1.0;
	}
	else
	{
		saturation = 1.0;
		value = (1.0 - state->tone) * 2.0;
	}
	state->color = HsvToRgb(state->hue, saturation, value, state->color.alpha);
}

void SetEditText(HWND edit, const WCHAR* value)
{
	SetWindowText(edit, value);
}

void UpdateEdits(PickerState* state)
{
	state->updatingEdits = true;
	WCHAR value[16];
	std::swprintf(value, _countof(value), L"#%02X%02X%02X",
		state->color.red, state->color.green, state->color.blue);
	SetEditText(state->edits[0], value);

	const int components[] =
	{
		state->color.red, state->color.green, state->color.blue, state->color.alpha
	};
	for (int i = 0; i < 4; ++i)
	{
		std::swprintf(value, _countof(value), L"%i", components[i]);
		SetEditText(state->edits[i + 1], value);
	}
	state->updatingEdits = false;
}

bool ParseHex(const WCHAR* text, LumeWidgetColor& color)
{
	if (*text == L'#') ++text;
	if (wcslen(text) != 6) return false;
	WCHAR* end = nullptr;
	const unsigned long value = wcstoul(text, &end, 16);
	if (!end || *end != L'\0') return false;
	color.red = (BYTE)((value >> 16) & 0xff);
	color.green = (BYTE)((value >> 8) & 0xff);
	color.blue = (BYTE)(value & 0xff);
	return true;
}

int ReadEditInteger(HWND edit, int fallback)
{
	WCHAR value[16];
	GetWindowText(edit, value, _countof(value));
	WCHAR* end = nullptr;
	const long parsed = wcstol(value, &end, 10);
	return end != value && *end == L'\0' ? (int)parsed : fallback;
}

void ParseEdits(PickerState* state)
{
	if (state->lastColorEdit == kEditHex)
	{
		WCHAR value[16];
		GetWindowText(state->edits[0], value, _countof(value));
		ParseHex(value, state->color);
	}
	else
	{
		state->color.red = ClampByte(ReadEditInteger(state->edits[1], state->color.red));
		state->color.green = ClampByte(ReadEditInteger(state->edits[2], state->color.green));
		state->color.blue = ClampByte(ReadEditInteger(state->edits[3], state->color.blue));
	}
	state->color.alpha = ClampByte(ReadEditInteger(
		state->edits[4], state->color.alpha));
	SyncModelFromColor(state);
	UpdateEdits(state);
}

void DrawText(
	Gdiplus::Graphics& graphics,
	const WCHAR* text,
	float size,
	Gdiplus::RectF rect,
	Gdiplus::Color color,
	Gdiplus::StringAlignment alignment = Gdiplus::StringAlignmentCenter)
{
	lume::platform::windows::direct_write::Style style;
	style.size = size;
	style.flags |= alignment == Gdiplus::StringAlignmentCenter ? DT_CENTER :
		alignment == Gdiplus::StringAlignmentFar ? DT_RIGHT : DT_LEFT;
	lume::platform::windows::direct_write::Draw(graphics, text, rect, color, style);
}

void AddRoundedRectanglePath(
	Gdiplus::GraphicsPath& path,
	Gdiplus::RectF rect,
	float radius)
{
	const float diameter = radius * 2.0f;
	path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
	path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
	path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter,
		diameter, diameter, 0.0f, 90.0f);
	path.AddArc(rect.X, rect.GetBottom() - diameter,
		diameter, diameter, 90.0f, 90.0f);
	path.CloseFigure();
}

void DrawRoundedRectangle(
	Gdiplus::Graphics& graphics,
	Gdiplus::Pen* pen,
	Gdiplus::RectF rect,
	float radius)
{
	Gdiplus::GraphicsPath path;
	AddRoundedRectanglePath(path, rect, radius);
	graphics.DrawPath(pen, &path);
}

void FillRoundedRectangle(
	Gdiplus::Graphics& graphics,
	Gdiplus::Brush* brush,
	Gdiplus::RectF rect,
	float radius)
{
	Gdiplus::GraphicsPath path;
	AddRoundedRectanglePath(path, rect, radius);
	graphics.FillPath(brush, &path);
}

void DrawHueRing(Gdiplus::Graphics& graphics, const PickerState* state)
{
	constexpr float centerX = 160.0f;
	constexpr float centerY = 150.0f;
	constexpr float innerRadius = 74.0f;
	constexpr float outerRadius = 112.0f;
	for (int degree = 0; degree < 360; ++degree)
	{
		const double angle = (degree - 90) * 3.14159265358979323846 / 180.0;
		const LumeWidgetColor hueColor = HsvToRgb(degree / 360.0, 1.0, 1.0, 255);
		Gdiplus::Pen pen(ToGdiColor(hueColor), 2.2f);
		graphics.DrawLine(&pen,
			centerX + (float)cos(angle) * innerRadius,
			centerY + (float)sin(angle) * innerRadius,
			centerX + (float)cos(angle) * outerRadius,
			centerY + (float)sin(angle) * outerRadius);
	}

	Gdiplus::SolidBrush centerBrush(ToGdiColor(state->color));
	graphics.FillEllipse(&centerBrush, centerX - 63.0f, centerY - 63.0f, 126.0f, 126.0f);
	Gdiplus::Pen centerBorder(Gdiplus::Color(255, 245, 245, 245), 3.0f);
	graphics.DrawEllipse(&centerBorder, centerX - 65.0f, centerY - 65.0f, 130.0f, 130.0f);

	const double markerAngle = (state->hue * 360.0 - 90.0) *
		3.14159265358979323846 / 180.0;
	const float markerX = centerX + (float)cos(markerAngle) * 94.0f;
	const float markerY = centerY + (float)sin(markerAngle) * 94.0f;
	Gdiplus::Pen marker(Gdiplus::Color(255, 255, 255, 255), 3.0f);
	graphics.DrawEllipse(&marker, markerX - 14.0f, markerY - 14.0f, 28.0f, 28.0f);
	Gdiplus::Pen markerShadow(Gdiplus::Color(100, 0, 0, 0), 1.0f);
	graphics.DrawEllipse(&markerShadow, markerX - 15.0f, markerY - 15.0f, 30.0f, 30.0f);
}

void DrawValueSlider(Gdiplus::Graphics& graphics, const PickerState* state)
{
	const float x = 30.0f;
	const float y = 310.0f;
	const float width = 260.0f;
	const float height = 24.0f;
	for (int pixel = 0; pixel < (int)width; ++pixel)
	{
		const double tone = pixel / (width - 1.0);
		double saturation;
		double value;
		if (tone <= 0.5)
		{
			saturation = tone * 2.0;
			value = 1.0;
		}
		else
		{
			saturation = 1.0;
			value = (1.0 - tone) * 2.0;
		}
		Gdiplus::Pen pen(ToGdiColor(HsvToRgb(state->hue, saturation, value, 255)));
		graphics.DrawLine(&pen, x + pixel, y, x + pixel, y + height);
	}
	Gdiplus::Pen border(Gdiplus::Color(255, 200, 200, 200));
	DrawRoundedRectangle(graphics, &border, Gdiplus::RectF(x, y, width, height), 5.0f);
	const float markerX = x + (float)state->tone * width;
	Gdiplus::SolidBrush marker(Gdiplus::Color(255, 255, 255, 255));
	FillRoundedRectangle(graphics, &marker,
		Gdiplus::RectF(markerX - 7.0f, y - 1.0f, 14.0f, height + 2.0f), 4.0f);
	DrawRoundedRectangle(graphics, &border,
		Gdiplus::RectF(markerX - 7.0f, y - 1.0f, 14.0f, height + 2.0f), 4.0f);
}

void DrawCheckerboard(Gdiplus::Graphics& graphics, Gdiplus::RectF rect)
{
	Gdiplus::SolidBrush light(Gdiplus::Color(255, 245, 245, 245));
	Gdiplus::SolidBrush dark(Gdiplus::Color(255, 211, 211, 211));
	graphics.FillRectangle(&light, rect);
	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 26; ++column)
		{
			if ((row + column) % 2 == 0)
			{
				graphics.FillRectangle(&dark,
					rect.X + column * 10.0f,
					rect.Y + row * 8.0f,
					10.0f,
					8.0f);
			}
		}
	}
}

void DrawAlphaSlider(Gdiplus::Graphics& graphics, const PickerState* state)
{
	const Gdiplus::RectF rect(30.0f, 350.0f, 260.0f, 24.0f);
	DrawCheckerboard(graphics, rect);
	for (int pixel = 0; pixel < (int)rect.Width; ++pixel)
	{
		const BYTE alpha = (BYTE)std::lround(pixel * 255.0 / (rect.Width - 1.0));
		Gdiplus::Pen pen(ToGdiColor(state->color, alpha));
		graphics.DrawLine(&pen,
			rect.X + pixel, rect.Y, rect.X + pixel, rect.GetBottom());
	}
	Gdiplus::Pen border(Gdiplus::Color(255, 200, 200, 200));
	DrawRoundedRectangle(graphics, &border, rect, 5.0f);
	const float markerX = rect.X + state->color.alpha * rect.Width / 255.0f;
	Gdiplus::SolidBrush marker(Gdiplus::Color(255, 255, 255, 255));
	FillRoundedRectangle(graphics, &marker,
		Gdiplus::RectF(markerX - 7.0f, rect.Y - 1.0f, 14.0f, rect.Height + 2.0f), 4.0f);
	DrawRoundedRectangle(graphics, &border,
		Gdiplus::RectF(markerX - 7.0f, rect.Y - 1.0f, 14.0f, rect.Height + 2.0f), 4.0f);
}

void DrawPresets(Gdiplus::Graphics& graphics, const PickerState*)
{
	for (int i = 0; i < (int)_countof(kPresetColors); ++i)
	{
		const int row = i / 7;
		const int column = i % 7;
		const float centerX = 45.0f + column * 38.5f;
		const float centerY = 482.0f + row * 40.0f;
		const LumeWidgetColor color =
		{
			GetRValue(Preset(i)), GetGValue(Preset(i)),
			GetBValue(Preset(i)), 255
		};
		Gdiplus::SolidBrush brush(ToGdiColor(color));
		graphics.FillEllipse(&brush, centerX - 14.0f, centerY - 14.0f, 28.0f, 28.0f);
		Gdiplus::Pen border(Gdiplus::Color(255, 100, 100, 100), 0.8f);
		graphics.DrawEllipse(&border, centerX - 14.0f, centerY - 14.0f, 28.0f, 28.0f);
	}
}

void DrawPicker(PickerState* state, HDC hdc)
{
	RECT client;
	GetClientRect(state->window, &client);
	Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
	Gdiplus::Graphics graphics(&buffer);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.ScaleTransform((Gdiplus::REAL)state->scale, (Gdiplus::REAL)state->scale);

	Gdiplus::SolidBrush background(Gdiplus::Color(255, 255, 255, 255));
	graphics.FillRectangle(&background, 0.0f, 0.0f,
		(float)kLogicalWidth, (float)kLogicalHeight);
	DrawText(graphics, L"\u9009\u62e9\u989c\u8272", 18.0f,
		Gdiplus::RectF(55.0f, 12.0f, 210.0f, 34.0f), Gdiplus::Color(255, 30, 30, 30));
	DrawText(graphics, L"\u00d7", 28.0f,
		Gdiplus::RectF(280.0f, 9.0f, 28.0f, 34.0f), Gdiplus::Color(255, 80, 80, 80));

	DrawHueRing(graphics, state);
	DrawValueSlider(graphics, state);
	DrawAlphaSlider(graphics, state);

	const WCHAR* labels[] = { L"HEX", L"\u7ea2", L"\u7eff", L"\u84dd", L"\u900f\u660e" };
	const float fieldX[] = { 28.0f, 101.0f, 148.0f, 195.0f, 242.0f };
	const float fieldW[] = { 66.0f, 40.0f, 40.0f, 40.0f, 50.0f };
	for (int i = 0; i < 5; ++i)
	{
		DrawText(graphics, labels[i], 12.0f,
			Gdiplus::RectF(fieldX[i], 432.0f, fieldW[i], 24.0f),
			Gdiplus::Color(255, 70, 70, 70));
	}

	Gdiplus::Pen divider(Gdiplus::Color(255, 225, 225, 225));
	graphics.DrawLine(&divider, 28.0f, 462.0f, 292.0f, 462.0f);
	graphics.DrawLine(&divider, 28.0f, 556.0f, 292.0f, 556.0f);
	DrawPresets(graphics, state);

	Gdiplus::SolidBrush cancelBrush(Gdiplus::Color(255, 255, 255, 255));
	Gdiplus::Pen cancelBorder(Gdiplus::Color(255, 190, 190, 190));
	FillRoundedRectangle(graphics, &cancelBrush,
		Gdiplus::RectF(148.0f, 572.0f, 72.0f, 34.0f), 4.0f);
	DrawRoundedRectangle(graphics, &cancelBorder,
		Gdiplus::RectF(148.0f, 572.0f, 72.0f, 34.0f), 4.0f);
	DrawText(graphics, L"\u53d6\u6d88", 14.0f,
		Gdiplus::RectF(148.0f, 572.0f, 72.0f, 34.0f), Gdiplus::Color(255, 50, 50, 50));

	Gdiplus::SolidBrush acceptBrush(Gdiplus::Color(255, 0, 0, 0));
	FillRoundedRectangle(graphics, &acceptBrush,
		Gdiplus::RectF(228.0f, 572.0f, 64.0f, 34.0f), 4.0f);
	DrawText(graphics, L"\u786e\u5b9a", 14.0f,
		Gdiplus::RectF(228.0f, 572.0f, 64.0f, 34.0f), Gdiplus::Color(255, 255, 255, 255));

	lume::platform::windows::RoundedWindowPaint output(hdc,client.right,client.bottom);
	{ Gdiplus::Graphics target(output.Dc()); target.DrawImage(&buffer, 0, 0); }
	output.Present(static_cast<float>(7.0*state->scale));
}

void InvalidatePicker(PickerState* state)
{
	UpdateEdits(state);
	InvalidateRect(state->window, nullptr, FALSE);
}

void SetHueFromPoint(PickerState* state, double x, double y)
{
	state->hue = atan2(y - 150.0, x - 160.0) /
		(2.0 * 3.14159265358979323846) + 0.25;
	if (state->hue < 0.0) state->hue += 1.0;
	if (state->hue >= 1.0) state->hue -= 1.0;
	if (state->tone <= 0.01 || state->tone >= 0.99)
	{
		state->tone = 0.5;
	}
	SyncColorFromModel(state);
	InvalidatePicker(state);
}

void SetToneFromPoint(PickerState* state, double x)
{
	state->tone = min(1.0, max(0.0, (x - 30.0) / 260.0));
	SyncColorFromModel(state);
	InvalidatePicker(state);
}

void SetAlphaFromPoint(PickerState* state, double x)
{
	const double alpha = min(1.0, max(0.0, (x - 30.0) / 260.0));
	state->color.alpha = (BYTE)std::lround(alpha * 255.0);
	InvalidatePicker(state);
}

LRESULT CALLBACK EditSubclassProc(
	HWND window,
	UINT message,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR subclassId,
	DWORD_PTR reference)
{
	if (message == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE))
	{
		return SendMessage((HWND)reference, message, wParam, lParam);
	}
	if (message == WM_NCDESTROY)
	{
		RemoveWindowSubclass(window, EditSubclassProc, subclassId);
	}
	return DefSubclassProc(window, message, wParam, lParam);
}

bool CreateEditControls(PickerState* state)
{
	const int ids[] = { kEditHex, kEditRed, kEditGreen, kEditBlue, kEditAlpha };
	const int logicalX[] = { 28, 101, 148, 195, 242 };
	const int logicalW[] = { 66, 40, 40, 40, 50 };
	state->editFont = CreateFont(
		-Scale(state, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, lume::platform::windows::ui_font::Family());

	for (int i = 0; i < 5; ++i)
	{
		state->edits[i] = CreateWindowEx(
			0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
			WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
			Scale(state, logicalX[i]), Scale(state, 394),
			Scale(state, logicalW[i]), Scale(state, 34),
			state->window, (HMENU)(INT_PTR)ids[i], GetModuleHandle(nullptr), nullptr);
		SendMessage(state->edits[i], WM_SETFONT, (WPARAM)state->editFont, TRUE);
		if (!lume::platform::windows::direct_write_edit::Attach(state->edits[i])) return false;
		SendMessage(state->edits[i], EM_SETLIMITTEXT, i == 0 ? 7 : 3, 0);
		SetWindowSubclass(
			state->edits[i], EditSubclassProc, 1, (DWORD_PTR)state->window);
	}
	UpdateEdits(state);
	return true;
}

void AcceptPicker(PickerState* state)
{
	ParseEdits(state);
	state->accepted = true;
	DestroyWindow(state->window);
}

LRESULT CALLBACK PickerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	PickerState* state = (PickerState*)GetWindowLongPtr(window, GWLP_USERDATA);
	if (message == WM_NCCREATE)
	{
		state = (PickerState*)((CREATESTRUCT*)lParam)->lpCreateParams;
		SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)state);
		state->window = window;
	}
	if (!state) return DefWindowProc(window, message, wParam, lParam);

	switch (message)
	{
	case WM_CREATE:
		return CreateEditControls(state) ? 0 : -1;

	case WM_COMMAND:
		if (HIWORD(wParam) == EN_CHANGE && !state->updatingEdits &&
			LOWORD(wParam) >= kEditHex && LOWORD(wParam) <= kEditBlue)
		{
			// Alpha is independent of the active HEX/RGB color input.
			state->lastColorEdit = LOWORD(wParam);
		}
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC hdc = BeginPaint(window, &paint);
		DrawPicker(state, hdc);
		EndPaint(window, &paint);
		return 0;
	}

	case WM_LBUTTONDOWN:
	{
		SetFocus(window);
		const double x = GET_X_LPARAM(lParam) / state->scale;
		const double y = GET_Y_LPARAM(lParam) / state->scale;
		if (x >= 278.0 && y <= 48.0)
		{
			DestroyWindow(window);
			return 0;
		}

		const double dx = x - 160.0;
		const double dy = y - 150.0;
		const double distance = sqrt(dx * dx + dy * dy);
		if (distance >= 72.0 && distance <= 116.0)
		{
			state->draggingHue = true;
			SetCapture(window);
			SetHueFromPoint(state, x, y);
			return 0;
		}
		if (x >= 23.0 && x <= 297.0 && y >= 303.0 && y <= 341.0)
		{
			state->draggingTone = true;
			SetCapture(window);
			SetToneFromPoint(state, x);
			return 0;
		}
		if (x >= 23.0 && x <= 297.0 && y >= 343.0 && y <= 381.0)
		{
			ParseEdits(state);
			state->draggingAlpha = true;
			SetCapture(window);
			SetAlphaFromPoint(state, x);
			return 0;
		}

		for (int i = 0; i < (int)_countof(kPresetColors); ++i)
		{
			const double swatchX = 45.0 + (i % 7) * 38.5;
			const double swatchY = 482.0 + (i / 7) * 40.0;
			const double swatchDx = x - swatchX;
			const double swatchDy = y - swatchY;
			if (swatchDx * swatchDx + swatchDy * swatchDy <= 225.0)
			{
				state->color.red = GetRValue(Preset(i));
				state->color.green = GetGValue(Preset(i));
				state->color.blue = GetBValue(Preset(i));
				SyncModelFromColor(state);
				InvalidatePicker(state);
				return 0;
			}
		}

		if (x >= 148.0 && x <= 220.0 && y >= 568.0 && y <= 610.0)
		{
			DestroyWindow(window);
			return 0;
		}
		if (x >= 228.0 && x <= 294.0 && y >= 568.0 && y <= 610.0)
		{
			AcceptPicker(state);
			return 0;
		}
		return 0;
	}

	case WM_MOUSEMOVE:
		if (GetCapture() == window)
		{
			const double x = GET_X_LPARAM(lParam) / state->scale;
			const double y = GET_Y_LPARAM(lParam) / state->scale;
			if (state->draggingHue) SetHueFromPoint(state, x, y);
			if (state->draggingTone) SetToneFromPoint(state, x);
			if (state->draggingAlpha) SetAlphaFromPoint(state, x);
		}
		return 0;

	case WM_LBUTTONUP:
		state->draggingHue = false;
		state->draggingTone = false;
		state->draggingAlpha = false;
		if (GetCapture() == window) ReleaseCapture();
		return 0;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
		{
			DestroyWindow(window);
			return 0;
		}
		if (wParam == VK_RETURN)
		{
			AcceptPicker(state);
			return 0;
		}
		break;

	case WM_CLOSE:
		DestroyWindow(window);
		return 0;

	case WM_DESTROY:
		if (state->editFont)
		{
			DeleteObject(state->editFont);
			state->editFont = nullptr;
		}
		state->done = true;
		return 0;
	}

	return DefWindowProc(window, message, wParam, lParam);
}

bool RegisterPickerClass()
{
	WNDCLASSEX windowClass{}; windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = PickerWindowProc;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	windowClass.lpszClassName = kWindowClass;
	return RegisterClassEx(&windowClass) != 0 ||
		GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

UINT GetWindowDpi(HWND window)
{
return window ? GetDpiForWindow(window) : 96;
}
}

bool LumeWidgetColorPicker::Show(HWND owner, LumeWidgetColor& color, COLORREF defaultColor)
{
	firstPreset = defaultColor;
	if (!RegisterPickerClass()) return false;

	PickerState state;
	state.owner = owner;
	state.color = color;
	SyncModelFromColor(&state);

	POINT cursor;
	GetCursorPos(&cursor);
	HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo{}; monitorInfo.cbSize = sizeof(monitorInfo);
	GetMonitorInfo(monitor, &monitorInfo);
	const double workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
	const double workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
	const UINT dpi = GetWindowDpi(owner);
	const double dpiScale = max(1.0, dpi / 96.0);
	const double fitScaleX = (workWidth - 16.0) / kLogicalWidth;
	const double fitScaleY = (workHeight - 16.0) / kLogicalHeight;
	state.scale = max(0.5, min(dpiScale, min(fitScaleX, fitScaleY)));
	const int width = Scale(&state, kLogicalWidth);
	const int height = Scale(&state, kLogicalHeight);

	int x = cursor.x + Scale(&state, 12);
	int y = cursor.y - Scale(&state, 80);
	x = min(max(x, monitorInfo.rcWork.left), monitorInfo.rcWork.right - width);
	y = min(max(y, monitorInfo.rcWork.top), monitorInfo.rcWork.bottom - height);

	state.window = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kWindowClass,
		L"",
		WS_POPUP,
		x, y, width, height,
		owner, nullptr, GetModuleHandle(nullptr), &state);
	if (!state.window) return false;
	const bool restoreOwner = owner && IsWindow(owner) && IsWindowEnabled(owner);
	if (restoreOwner)
	{
		EnableWindow(owner, FALSE);
	}

	lume::platform::windows::ConfigureRoundedWindow(state.window,static_cast<float>(7.0*state.scale));
	ShowWindow(state.window, SW_SHOWNORMAL);
	SetForegroundWindow(state.window);

	MSG message;
	while (!state.done)
	{
		const BOOL result = GetMessage(&message, nullptr, 0, 0);
		if (result <= 0)
		{
			if (IsWindow(state.window))
			{
				DestroyWindow(state.window);
			}
			if (result == 0) PostQuitMessage((int)message.wParam);
			break;
		}
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	if (restoreOwner && IsWindow(owner))
	{
		EnableWindow(owner, TRUE);
	}

	if (state.accepted)
	{
		color = state.color;
		return true;
	}
	return false;
}
