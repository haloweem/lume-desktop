#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace lume::platform::windows {
struct UpdateNotice {
    // manual 且 version 为空表示“检查更新”结果，detail 由 shell 提供本地化说明。
    std::wstring version, detail;
    bool manual = false;
};

// 所有调用均在创建它的 UI 线程。调度、语言选择和持久化由 shell 负责。
// Show 成功表示内容已绘制且窗口实际可见；玻璃首帧前或失败时使用可读实色。
// 自动检查禁止创建或改变窗口；只有 manual 结果可以展示。
// 重复 Show 更新同一窗口并重置十秒计时。Shutdown 幂等且不可恢复。
class UpdatePresenter final {
public:
    UpdatePresenter(HINSTANCE, std::function<void()> openWebsite);
    ~UpdatePresenter();
    UpdatePresenter(const UpdatePresenter&) = delete;
    UpdatePresenter& operator=(const UpdatePresenter&) = delete;
    bool Show(const UpdateNotice&);
    void Shutdown();
    bool HasVisible() const;
    HWND Handle() const;
    void SetLanguage(bool english);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend struct UpdatePresenterTestAccess;
};

// 仅隔离测试使用；不改变系统工作区/DPI，不注入官网地址或材质实现。
struct UpdatePresenterTestAccess {
    static bool Environment(UpdatePresenter&, RECT workArea, UINT dpi,
                            UINT lifetimeMs = 10000, UINT materialTimeoutMs = 3000);
    // 1=官网，2=稍后，3=关闭，4=说明滚动条；返回客户区物理像素。
    static RECT ActionRect(const UpdatePresenter&, unsigned action);
    static bool CanScroll(const UpdatePresenter&);
    static float ScrollOffset(const UpdatePresenter&);
    static UINT_PTR DismissTimer(const UpdatePresenter&);
    static BYTE PixelAlpha(const UpdatePresenter&, int x, int y);
};
}
