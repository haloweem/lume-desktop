#include "platform/windows/organizer/shell_drag.h"
#include "platform/windows/lume_window_ids.h"
#include <ole2.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <cwchar>

namespace lume::platform::windows {
bool OverDesktop(POINT screen) {
    const HWND root = GetAncestor(WindowFromPoint(screen), GA_ROOT);
    wchar_t name[32]{};
    if (!root || !GetClassNameW(root, name, 32) || (wcscmp(name, L"Progman") && wcscmp(name, L"WorkerW"))) return false;
    DWORD shell = 0, owner = 0;
    GetWindowThreadProcessId(GetShellWindow(), &shell);
    GetWindowThreadProcessId(root, &owner);
    return shell && shell == owner;
}
bool OverProgram(POINT screen) {
    const HWND root = GetAncestor(WindowFromPoint(screen), GA_ROOT);
    if (!root || OverDesktop(screen)) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(root, &owner);
    if (owner == GetCurrentProcessId()) return GetPropW(root, lume::window_ids::DesktopShelfProp) != nullptr;
    wchar_t name[32]{};
    return !(GetClassNameW(root, name, 32) &&
             (!wcscmp(name, L"Shell_TrayWnd") || !wcscmp(name, L"Shell_SecondaryTrayWnd")));
}
namespace {
bool PointerOverDesktop() {
    POINT cursor{};
    return GetCursorPos(&cursor) && OverDesktop(cursor);
}
// The default drop source, except that a release over the desktop cancels.
class DesktopDropSource final : public IDropSource {
public:
    bool desktop = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == IID_IUnknown || id == IID_IDropSource) { *out = static_cast<IDropSource*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }  // stack owned
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (keys & MK_LBUTTON) return S_OK;
        if (PointerOverDesktop()) { desktop = true; return DRAGDROP_S_CANCEL; }
        return DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        // Over the desktop a release does not copy, so show no copy cursor.
        if (!PointerOverDesktop()) return DRAGDROP_S_USEDEFAULTCURSORS;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return S_OK;
    }
private:
    ULONG references_ = 1;
};
DragOutResult Drag(HWND source, const std::vector<std::wstring>& paths, bool desktopTakesBack) {
    using Microsoft::WRL::ComPtr;
    if (paths.empty()) return {};
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole) && ole != RPC_E_CHANGED_MODE) return {};
    struct Ole { HRESULT result; ~Ole() { if (SUCCEEDED(result)) OleUninitialize(); } } oleLease{ole};
    std::vector<PIDLIST_ABSOLUTE> items;
    struct Items {
        std::vector<PIDLIST_ABSOLUTE>& value;
        ~Items() { for (auto item : value) CoTaskMemFree(item); }
    } itemsLease{items};
    for (const auto& path : paths) {
        if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        PIDLIST_ABSOLUTE item = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr)) && item) items.push_back(item);
    }
    if (items.empty()) return {};
    ComPtr<IShellItemArray> array;
    ComPtr<IDataObject> data;
    if (FAILED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(items.size()),
            const_cast<PCIDLIST_ABSOLUTE_ARRAY>(items.data()), &array)) ||
        FAILED(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))))
        return {};
    DesktopDropSource desktopSource;
    DragOutResult result;
    // A null drop source uses the shell's own, which also shows the drag image.
    const HRESULT dropped = SHDoDragDrop(source, data.Get(), desktopTakesBack ? &desktopSource : nullptr,
                                         DROPEFFECT_COPY | DROPEFFECT_LINK, &result.effect);
    result.desktop = desktopSource.desktop;
    if (dropped != DRAGDROP_S_DROP) result.effect = DROPEFFECT_NONE;
    return result;
}
}
DWORD DragPathsOut(HWND source, const std::vector<std::wstring>& paths) {
    return Drag(source, paths, false).effect;
}
DragOutResult DragPathsOutToDesktop(HWND source, const std::vector<std::wstring>& paths) {
    return Drag(source, paths, true);
}
}
