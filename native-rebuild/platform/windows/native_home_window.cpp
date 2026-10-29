#include "platform/windows/native_home_window.h"
#include "platform/windows/widget_ui/direct_write_text.h"

#include <algorithm>
#include <array>
#include <utility>

namespace lume::platform::windows {
namespace {

constexpr wchar_t kWindowClass[] = L"LumeNativeHomeWindow";
constexpr UINT_PTR kClockTimer = 1;
constexpr COLORREF kBackground = RGB(12, 14, 17);
constexpr COLORREF kPanel = RGB(22, 25, 30);
constexpr COLORREF kBorder = RGB(54, 60, 69);
constexpr COLORREF kText = RGB(242, 244, 247);
constexpr COLORREF kMuted = RGB(157, 165, 176);
constexpr COLORREF kAccent = RGB(65, 153, 235);

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void RoundPanel(HDC dc, RECT rect, COLORREF fill, COLORREF stroke, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Text(HDC dc, const wchar_t* value, RECT rect, int size, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    direct_write::Style style;
    style.size = static_cast<float>(size);
    style.weight = size >= 22 ? FW_SEMIBOLD : FW_NORMAL;
    style.flags = flags | DT_NOPREFIX;
    direct_write::Draw(dc, value, rect, color, style);
}

void CenteredText(HDC dc, const wchar_t* value, RECT rect, int size, COLORREF color) {
    Text(dc, value, rect, size, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

}  // namespace

NativeHomeWindow::~NativeHomeWindow() { Close(); }

void NativeHomeWindow::Close() {
    if (IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    if (instance_) UnregisterClassW(kWindowClass, instance_);
}

bool NativeHomeWindow::Create(HINSTANCE instance, ui::HomeScreenModel& model,
                          std::function<void(const ui::HomeScreenSnapshot&)> onChanged) {
    instance_ = instance;
    model_ = &model;
    onChanged_ = std::move(onChanged);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &NativeHomeWindow::WindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_LUME_APP));
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    window_ = CreateWindowExW(0, kWindowClass, L"Lume", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1180, 790, nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

int NativeHomeWindow::Run(HINSTANCE instance, ui::HomeScreenModel& model,
                          std::function<void(const ui::HomeScreenSnapshot&)> onChanged) {
    if (!Create(instance, model, std::move(onChanged))) return 1;
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window_, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Close();
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK NativeHomeWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeHomeWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<NativeHomeWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT NativeHomeWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 900;
        info->ptMinTrackSize.y = 700;
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) ApplyAction(LOWORD(wParam));
        return 0;
    case WM_DRAWITEM:
        DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_TIMER:
        if (wParam == kClockTimer) {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            if (lastMinute_ != now.wMinute) {
                lastMinute_ = now.wMinute;
                RECT preview{304, 228, 598, 380};
                InvalidateRect(window, &preview, FALSE);
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        Draw(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kClockTimer);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void NativeHomeWindow::CreateControls() {
    static constexpr std::array<int, 8> ids = {HomeButton, BeautyButton, WallpaperButton, ClockCardButton,
                                                ClockEnabledButton, WelfareButton, MondButton, InsideDreamButton};
    static constexpr std::array<const wchar_t*, 8> labels = {L"首页", L"桌面美化", L"壁纸", L"时钟设置",
                                                              L"启用时钟", L"样式一", L"样式二", L"样式三"};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        controls_[i] = CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                       0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
                                       instance_, nullptr);
    }
    controls_[8] = CreateWindowExW(0, L"BUTTON", L"返回组件", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                   0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BackButton)),
                                   instance_, nullptr);
    for (int index = 9; index < 13; ++index) {
        controls_[index] = CreateWindowExW(0, L"BUTTON", L"打开", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(150 + index - 9)), instance_, nullptr);
    }
    UpdateControls();
}

void NativeHomeWindow::LayoutControls() {
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int left = 42;
    const int navY = 22;
    MoveWindow(controls_[0], width - 390, navY, 82, 38, TRUE);
    MoveWindow(controls_[1], width - 300, navY, 110, 38, TRUE);
    MoveWindow(controls_[2], width - 180, navY, 92, 38, TRUE);
    MoveWindow(controls_[3], width - 224, 284, 160, 34, TRUE);
    MoveWindow(controls_[4], width - 236, 180, 160, 36, TRUE);
    const int styleWidth = (width - 274 - 42 - 60 - 24) / 3;
    MoveWindow(controls_[5], 304, 476, styleWidth, 70, TRUE);
    MoveWindow(controls_[6], 304 + styleWidth + 12, 476, styleWidth, 70, TRUE);
    MoveWindow(controls_[7], 304 + 2 * (styleWidth + 12), 476, styleWidth, 70, TRUE);
    MoveWindow(controls_[8], left + 22, 99, 120, 38, TRUE);
    const int cardWidth = (width - 84 - 36) / 3;
    for (int i = 0; i < 3; ++i) MoveWindow(controls_[9 + i], 62 + i * (cardWidth + 18), 536, 76, 28, TRUE);
    MoveWindow(controls_[12], 60, 486, 76, 28, TRUE);
    UpdateControls();
}

void NativeHomeWindow::UpdateControls() {
    if (!model_) return;
    const auto page = model_->Snapshot().page;
    const bool beauty = page == ui::HomePage::DesktopBeauty;
    const bool clock = page == ui::HomePage::ClockSettings;
    ShowWindow(controls_[0], SW_SHOW);
    ShowWindow(controls_[1], SW_SHOW);
    ShowWindow(controls_[2], SW_SHOW);
    ShowWindow(controls_[3], beauty ? SW_SHOW : SW_HIDE);
    for (int index = 4; index <= 7; ++index) ShowWindow(controls_[index], clock ? SW_SHOW : SW_HIDE);
    ShowWindow(controls_[8], clock ? SW_SHOW : SW_HIDE);
    for (int i = 9; i < 12; ++i) ShowWindow(controls_[i], page == ui::HomePage::Home ? SW_SHOW : SW_HIDE);
    ShowWindow(controls_[12], beauty ? SW_SHOW : SW_HIDE);
    SetWindowTextW(controls_[4], model_->Snapshot().clockEnabled ? L"预览：已开启" : L"预览：已关闭");
    const auto style = model_->Snapshot().clockStyle;
    SetWindowTextW(controls_[5], style == ui::ClockStyle::Welfare ? L"样式一  ✓" : L"样式一");
    SetWindowTextW(controls_[6], style == ui::ClockStyle::Mond ? L"样式二  ✓" : L"样式二");
    SetWindowTextW(controls_[7], style == ui::ClockStyle::InsideDream ? L"样式三  ✓" : L"样式三");
    KillTimer(window_, kClockTimer);
    if (clock && model_->Snapshot().clockEnabled && IsWindowVisible(window_) && !IsIconic(window_)) {
        SetTimer(window_, kClockTimer, 1000, nullptr);
    }
}

void NativeHomeWindow::Draw(HWND window, HDC dc) {
    RECT client{};
    GetClientRect(window, &client);
    Fill(dc, client, kBackground);
    RECT brand{42, 18, 280, 66};
    Text(dc, L"LUME", brand, 25, kText);
    RECT tagline{42, 53, 300, 82};
    Text(dc, L"桌面体验工作台", tagline, 12, kMuted);
    RECT line{42, 78, client.right - 42, 79};
    Fill(dc, line, RGB(39, 44, 51));

    const auto state = model_->Snapshot();
    if (state.page == ui::HomePage::Home) {
        RECT hero{42, 116, client.right - 42, 350};
        RoundPanel(dc, hero, RGB(18, 22, 27), RGB(45, 52, 61), 20);
        RECT eyebrow{72, 154, client.right - 72, 180};
        Text(dc, L"LUME DESKTOP", eyebrow, 12, RGB(111, 177, 237));
        RECT title{72, 188, client.right - 72, 234};
        Text(dc, L"桌面体验，随心定制", title, 30, kText);
        RECT subtitle{74, 240, client.right - 72, 276};
        Text(dc, L"让时间、日历与灵感留在桌面", subtitle, 16, kMuted);
        RECT status{74, 303, client.right - 74, 328};
        Text(dc, state.clockEnabled ? L"时钟预览已开启" : L"时钟预览已关闭", status, 13, kMuted);

        Text(dc, L"快速进入", RECT{42, 390, 500, 424}, 19, kText);
        const int gap = 18;
        const int cardWidth = (client.right - 84 - 2 * gap) / 3;
        const std::array<const wchar_t*, 3> names = {L"桌面美化", L"壁纸", L"组件设置"};
        const std::array<const wchar_t*, 3> descriptions = {L"管理桌面组件与外观", L"动态壁纸与本地收藏", L"调整时钟显示样式"};
        for (int i = 0; i < 3; ++i) {
            RECT card{42 + i * (cardWidth + gap), 438, 42 + i * (cardWidth + gap) + cardWidth, 574};
            RoundPanel(dc, card, kPanel, kBorder, 14);
            RECT name{card.left + 20, card.top + 20, card.right - 18, card.top + 54};
            Text(dc, names[static_cast<std::size_t>(i)], name, 18, kText);
            RECT desc{card.left + 20, card.top + 62, card.right - 18, card.top + 104};
            Text(dc, descriptions[static_cast<std::size_t>(i)], desc, 13, kMuted);
        }
        RECT foot{42, client.bottom - 54, client.right - 42, client.bottom - 24};
        Text(dc, L"Lume 1.0.8 · 界面预览", foot, 11, RGB(112, 120, 131));
    } else if (state.page == ui::HomePage::DesktopBeauty) {
        RECT title{42, 110, client.right - 42, 152};
        Text(dc, L"桌面美化", title, 27, kText);
        RECT desc{42, 152, client.right - 42, 182};
        Text(dc, L"桌面组件", desc, 14, kMuted);
        RECT card{42, 198, client.right - 42, 334};
        RoundPanel(dc, card, kPanel, kBorder, 15);
        RECT titleBox{card.left + 22, card.top + 20, card.right - 20, card.top + 55};
        Text(dc, L"桌面组件", titleBox, 19, kText);
        RECT cardDesc{card.left + 22, card.top + 57, card.right - 20, card.top + 94};
        Text(dc, L"时间、日历、天气、黄历、性能、音乐与图标收纳", cardDesc, 13, kMuted);
        RECT stateBox{card.left + 22, card.top + 100, card.right - 20, card.top + 126};
        Text(dc, state.clockEnabled ? L"时钟预览：已开启" : L"时钟预览：已关闭", stateBox, 12, kMuted);
        RECT more{42, 364, client.right - 42, 392};
        Text(dc, L"组件", more, 15, kText);
        const int gap = 16;
        const int cardWidth = (client.right - 84 - 3 * gap) / 4;
        const std::array<const wchar_t*, 4> names = {L"时间", L"日历", L"天气", L"性能"};
        for (int i = 0; i < 4; ++i) {
            RECT item{42 + i * (cardWidth + gap), 408, 42 + i * (cardWidth + gap) + cardWidth, 526};
            RoundPanel(dc, item, kPanel, kBorder, 13);
            RECT label{item.left + 16, item.top + 18, item.right - 12, item.top + 50};
            Text(dc, names[static_cast<std::size_t>(i)], label, 16, kText);
            RECT availability{item.left + 16, item.top + 58, item.right - 12, item.bottom - 12};
            Text(dc, i == 0 ? L"可预览" : L"暂未接入", availability, 12, i == 0 ? RGB(120, 199, 155) : kMuted);
        }
    } else if (state.page == ui::HomePage::ClockSettings) {
        RECT title{194, 106, client.right - 42, 146};
        Text(dc, L"时钟设置", title, 25, kText);
        RECT left{42, 160, 250, client.bottom - 42};
        RoundPanel(dc, left, kPanel, kBorder, 14);
        RECT listTitle{left.left + 20, left.top + 24, left.right - 18, left.top + 58};
        Text(dc, L"组件", listTitle, 16, kText);
        RECT selected{left.left + 12, left.top + 76, left.right - 12, left.top + 122};
        RoundPanel(dc, selected, RGB(38, 52, 67), RGB(52, 112, 167), 9);
        RECT selectedText{selected.left + 14, selected.top + 5, selected.right - 8, selected.bottom - 5};
        Text(dc, L"时间", selectedText, 14, kText);

        RECT panel{274, 160, client.right - 42, client.bottom - 42};
        RoundPanel(dc, panel, kPanel, kBorder, 14);
        RECT panelTitle{panel.left + 30, panel.top + 22, panel.right - 28, panel.top + 54};
        Text(dc, L"时间 / CLOCK", panelTitle, 11, kMuted);
        RECT preview{panel.left + 30, panel.top + 68, panel.left + 324, panel.top + 220};
        RoundPanel(dc, preview, RGB(14, 17, 21), RGB(44, 50, 58), 12);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t timeText[16]{};
        wsprintfW(timeText, L"%02u:%02u", now.wHour, now.wMinute);
        RECT clockText{preview.left + 16, preview.top + 34, preview.right - 16, preview.top + 91};
        if (state.clockStyle == ui::ClockStyle::Welfare) {
            CenteredText(dc, state.clockEnabled ? timeText : L"预览已关闭", clockText, state.clockEnabled ? 36 : 18, kText);
        } else if (state.clockStyle == ui::ClockStyle::Mond) {
            CenteredText(dc, state.clockEnabled ? timeText : L"预览已关闭", clockText, state.clockEnabled ? 42 : 18, kText);
        } else {
            CenteredText(dc, state.clockEnabled ? timeText : L"预览已关闭", clockText, state.clockEnabled ? 30 : 18, RGB(167, 202, 228));
        }
        const wchar_t* weekdays[] = {L"SUNDAY", L"MONDAY", L"TUESDAY", L"WEDNESDAY", L"THURSDAY", L"FRIDAY", L"SATURDAY"};
        wchar_t dateLabel[64]{};
        wsprintfW(dateLabel, L"%s  ·  %02u/%02u", weekdays[now.wDayOfWeek], now.wMonth, now.wDay);
        RECT dateText{preview.left + 16, preview.top + 96, preview.right - 16, preview.top + 128};
        CenteredText(dc, dateLabel, dateText, 12, kMuted);
        RECT styleLabel{panel.left + 30, panel.top + 248, panel.right - 28, panel.top + 276};
        Text(dc, L"时间样式", styleLabel, 14, kText);
        RECT explanation{panel.left + 30, panel.top + 278, panel.right - 28, panel.top + 308};
        Text(dc, L"选择预览样式，桌面浮窗尚未接入", explanation, 12, kMuted);
        const std::array<std::pair<const wchar_t*, ui::ClockStyle>, 3> styles = {{{L"Welfare", ui::ClockStyle::Welfare},
                                                                                 {L"Mond", ui::ClockStyle::Mond},
                                                                                 {L"Inside Dream", ui::ClockStyle::InsideDream}}};
        const int styleGap = 12;
        const int styleWidth = (panel.right - panel.left - 60 - styleGap * 2) / 3;
        for (int i = 0; i < 3; ++i) {
            const int x = panel.left + 30 + i * (styleWidth + styleGap);
            RECT styleCard{x, panel.top + 316, x + styleWidth, panel.top + 386};
            const bool active = state.clockStyle == styles[static_cast<std::size_t>(i)].second;
            RoundPanel(dc, styleCard, active ? RGB(31, 54, 74) : RGB(25, 29, 34),
                       active ? kAccent : kBorder, 10);
            RECT styleText{styleCard.left + 10, styleCard.top + 8, styleCard.right - 10, styleCard.bottom - 8};
            CenteredText(dc, styles[static_cast<std::size_t>(i)].first, styleText, 12, active ? kText : kMuted);
        }
        RECT help{panel.left + 30, panel.bottom - 58, panel.right - 28, panel.bottom - 28};
        Text(dc, state.dirty ? L"设置尚未保存，请检查目录写入权限。" : L"设置保存在本机，仅用于当前窗口预览。", help, 11, kMuted);
    } else {
        RECT title{42, 112, client.right - 42, 154};
        Text(dc, L"壁纸", title, 27, kText);
        RECT panel{42, 176, client.right - 42, client.bottom - 42};
        RoundPanel(dc, panel, kPanel, kBorder, 15);
        RECT message{panel.left + 28, panel.top + 32, panel.right - 28, panel.top + 72};
        Text(dc, L"壁纸库正在迁移", message, 20, kText);
        RECT detail{panel.left + 28, panel.top + 78, panel.right - 28, panel.top + 120};
        Text(dc, L"动态壁纸服务将在后续阶段接入。", detail, 13, kMuted);
    }
}

void NativeHomeWindow::DrawButton(const DRAWITEMSTRUCT& item) const {
    const int id = static_cast<int>(item.CtlID);
    const auto state = model_->Snapshot();
    const bool selected = (id == BeautyButton && state.page == ui::HomePage::DesktopBeauty) ||
                          (id == WallpaperButton && state.page == ui::HomePage::WallpaperPlaceholder) ||
                          (id == HomeButton && state.page == ui::HomePage::Home) ||
                          (id == WelfareButton && state.clockStyle == ui::ClockStyle::Welfare) ||
                          (id == MondButton && state.clockStyle == ui::ClockStyle::Mond) ||
                          (id == InsideDreamButton && state.clockStyle == ui::ClockStyle::InsideDream);
    const COLORREF fill = selected ? RGB(37, 52, 67) : (item.itemState & ODS_SELECTED ? RGB(35, 41, 49) : kBackground);
    const COLORREF stroke = selected ? kAccent : kBackground;
    RECT bounds = item.rcItem;
    Fill(item.hDC, bounds, id >= ClockCardButton && id != BackButton ? kPanel : kBackground);
    RoundPanel(item.hDC, bounds, fill, stroke, 10);
    wchar_t label[64]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    CenteredText(item.hDC, label, bounds, 12, selected ? kText : RGB(204, 210, 217));
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&bounds, -4, -4);
        DrawFocusRect(item.hDC, &bounds);
    }
}

void NativeHomeWindow::ApplyAction(int controlId) {
    switch (controlId) {
    case HomeButton: model_->Navigate(ui::HomePage::Home); break;
    case BeautyButton: model_->Navigate(ui::HomePage::DesktopBeauty); break;
    case WallpaperButton: model_->Navigate(ui::HomePage::WallpaperPlaceholder); break;
    case ClockCardButton: model_->Navigate(ui::HomePage::ClockSettings); break;
    case QuickBeautyButton: model_->Navigate(ui::HomePage::DesktopBeauty); break;
    case QuickWallpaperButton: model_->Navigate(ui::HomePage::WallpaperPlaceholder); break;
    case QuickClockButton:
    case TimeCardButton: model_->Navigate(ui::HomePage::ClockSettings); break;
    case ClockEnabledButton: model_->SetClockEnabled(!model_->Snapshot().clockEnabled); break;
    case WelfareButton: model_->SetClockStyle(ui::ClockStyle::Welfare); break;
    case MondButton: model_->SetClockStyle(ui::ClockStyle::Mond); break;
    case InsideDreamButton: model_->SetClockStyle(ui::ClockStyle::InsideDream); break;
    case BackButton: model_->Navigate(ui::HomePage::DesktopBeauty); break;
    default: return;
    }
    UpdateControls();
    if (onChanged_) onChanged_(model_->Snapshot());
    InvalidateRect(window_, nullptr, FALSE);
    for (HWND control : controls_) InvalidateRect(control, nullptr, FALSE);
}

}  // namespace lume::platform::windows
