#include "platform/windows/organizer/organizer_drop_target.h"
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <atomic>
#include <cstring>
#include <limits>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr std::size_t kMaxPath = 32767;
// Bound external drag memory, not the number of ordinary file references.
constexpr std::size_t kMaxPayload = 64 * 1024 * 1024;

struct OleLease final {
    HRESULT result = OleInitialize(nullptr);
    ~OleLease() { if (SUCCEEDED(result)) OleUninitialize(); }
};

struct Medium final {
    STGMEDIUM value{};
    ~Medium() { if (value.tymed != TYMED_NULL || value.pUnkForRelease) ReleaseStgMedium(&value); }
};

struct GlobalLockGuard final {
    explicit GlobalLockGuard(HGLOBAL handle) : memory(handle), data(GlobalLock(handle)) {}
    ~GlobalLockGuard() { if (data) GlobalUnlock(memory); }
    HGLOBAL memory;
    void* data;
};

struct PathMemoryBudget final {
    bool Add(std::size_t bytes) {
        // Include the terminator, allocator bookkeeping, and vector storage
        // while growing (old, new, and spare string slots).
        constexpr std::size_t overhead = 1 + 3 * sizeof(std::string) + 2 * sizeof(void*);
        if (bytes > kMaxPayload - used || overhead > kMaxPayload - used - bytes) return false;
        used += bytes + overhead;
        return true;
    }
    std::size_t used = 0;
};

bool ValidShellIdList(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < sizeof(CIDA)) return false;
    UINT count = 0;
    std::memcpy(&count, bytes.data(), sizeof(count));
    if (!count || count > (bytes.size() - 2 * sizeof(UINT)) / sizeof(UINT)) return false;
    const auto headerBytes = (static_cast<std::size_t>(count) + 2) * sizeof(UINT);
    std::size_t scannedBytes = 0;
    PathMemoryBudget entries;
    for (std::size_t i = 0; i <= count; ++i) {
        // Reserve each child's metadata budget before the Shell constructs
        // its item array, including when many offsets alias a tiny PIDL.
        if (i && !entries.Add(0)) return false;
        UINT offset = 0;
        std::memcpy(&offset, bytes.data() + (i + 1) * sizeof(UINT), sizeof(offset));
        std::size_t at = offset;
        if (at < headerBytes) return false;
        for (;;) {
            if (at > bytes.size() || bytes.size() - at < sizeof(USHORT)) return false;
            USHORT length = 0;
            std::memcpy(&length, bytes.data() + at, sizeof(length));
            const auto consumed = length ? length : sizeof(length);
            if (consumed < sizeof(length) || consumed > bytes.size() - at ||
                consumed > kMaxPayload - scannedBytes) return false;
            scannedBytes += consumed;
            if (!length) break;
            at += length;
        }
    }
    return true;
}

class ShellIdListData final : public IDataObject {
public:
    ShellIdListData(CLIPFORMAT format, std::vector<unsigned char> bytes)
        : format_(format), bytes_(std::move(bytes)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        return format && format->cfFormat == format_ && !format->ptd &&
            format->dwAspect == DVASPECT_CONTENT && format->lindex == -1 &&
            (format->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!medium) return E_POINTER;
        *medium = {};
        if (QueryGetData(format) != S_OK) return DV_E_FORMATETC;
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes_.size());
        if (!memory) return E_OUTOFMEMORY;
        {
            GlobalLockGuard locked(memory);
            if (!locked.data) { GlobalFree(memory); return E_OUTOFMEMORY; }
            std::memcpy(locked.data, bytes_.data(), bytes_.size());
        }
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = memory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    ~ShellIdListData() = default;
    std::atomic<ULONG> references_{1};
    CLIPFORMAT format_;
    const std::vector<unsigned char> bytes_;
};

struct ShellShortcut final {
    ~ShellShortcut() { CoTaskMemFree(id); }
    bool Resolve(const std::wstring& parsingName);
    bool Save() const;
    PIDLIST_ABSOLUTE id = nullptr;
    std::wstring folder, file;
};

DWORD AllowedEffect(DWORD allowed) noexcept {
    if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
    if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    return DROPEFFECT_NONE;
}

bool ClientPixels(HWND window, POINTL screen, POINT& result) noexcept {
    // 取客户区原点的物理屏幕位置后相减，避免不感知 DPI 的窗口坐标往返舍入。
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!previous) return false;
    POINT origin{};
    const bool converted = ClientToScreen(window, &origin) != FALSE;
    SetThreadDpiAwarenessContext(previous);
    const auto x = static_cast<long long>(screen.x) - origin.x;
    const auto y = static_cast<long long>(screen.y) - origin.y;
    if (!converted || x < (std::numeric_limits<LONG>::min)() || x > (std::numeric_limits<LONG>::max)() ||
        y < (std::numeric_limits<LONG>::min)() || y > (std::numeric_limits<LONG>::max)()) return false;
    result = {static_cast<LONG>(x), static_cast<LONG>(y)};
    return true;
}

bool Utf8Path(const wchar_t* path, std::size_t length, std::vector<std::string>* paths,
              PathMemoryBudget* budget = nullptr) {
    if (!length || length > kMaxPath) return false;
    const int units = static_cast<int>(length);
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, units,
        nullptr, 0, nullptr, nullptr);
    if (!bytes || (budget && !budget->Add(static_cast<std::size_t>(bytes)))) return false;
    if (paths) {
        std::string text(static_cast<std::size_t>(bytes), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, units,
            text.data(), bytes, nullptr, nullptr) != bytes) return false;
        paths->push_back(std::move(text));
    }
    return true;
}

bool Utf8Path(const char* path, std::size_t length, std::vector<std::string>* paths,
              PathMemoryBudget* budget = nullptr) {
    if (!length || length > kMaxPath * sizeof(wchar_t)) return false;
    const int bytes = static_cast<int>(length);
    const int units = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, bytes, nullptr, 0);
    if (units <= 0 || static_cast<std::size_t>(units) > kMaxPath) return false;
    std::wstring wide(static_cast<std::size_t>(units), L'\0');
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, bytes, wide.data(), units) != units)
        return false;
    return Utf8Path(wide.data(), wide.size(), paths, budget);
}

template<class Char>
bool ReadList(const Char* list, std::size_t units, std::size_t& count,
              std::vector<std::string>* paths) {
    std::size_t at = 0, found = 0;
    PathMemoryBudget budget;
    const std::size_t maxUnits = sizeof(Char) == sizeof(wchar_t) ? kMaxPath : kMaxPath * sizeof(wchar_t);
    while (at < units && list[at]) {
        std::size_t length = 0;
        while (length < units - at && list[at + length] && length <= maxUnits) ++length;
        if (length == units - at || length > maxUnits || !Utf8Path(list + at, length, paths, &budget)) return false;
        ++found;
        at += length + 1;
    }
    if (!found || at >= units) return false;
    count = found;
    return true;
}

bool ReadShellPayload(IDataObject* data, std::size_t& count, std::vector<std::string>* paths, bool locations) {
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_SHELLIDLIST)), nullptr,
                      DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (!format.cfFormat || data->QueryGetData(&format) != S_OK) return false;
    std::vector<unsigned char> bytes;
    {
        Medium medium;
        if (FAILED(data->GetData(&format, &medium.value)) || medium.value.tymed != TYMED_HGLOBAL ||
            !medium.value.hGlobal) return false;
        const auto payloadBytes = GlobalSize(medium.value.hGlobal);
        if (payloadBytes < sizeof(CIDA) || payloadBytes > kMaxPayload) return false;
        GlobalLockGuard locked(medium.value.hGlobal);
        if (!locked.data) return false;
        const auto* begin = static_cast<const unsigned char*>(locked.data);
        bytes.assign(begin, begin + payloadBytes);
    }
    if (!ValidShellIdList(bytes)) return false;
    const auto payloadBytes = bytes.size();
    // Shell may call GetData again. Give it only the validated, owned snapshot,
    // never the external provider that could return a different second block.
    const std::shared_ptr<IDataObject> snapshot(new ShellIdListData(format.cfFormat, std::move(bytes)),
        [](IDataObject* value) { value->Release(); });
    IShellItemArray* array = nullptr;
    if (FAILED(SHCreateShellItemArrayFromDataObject(snapshot.get(), IID_PPV_ARGS(&array))) || !array) return false;
    struct ReleaseArray { IShellItemArray* value; ~ReleaseArray() { value->Release(); } } release{array};
    DWORD total = 0;
    // Every CIDA entry requires at least an offset. Do not trust a count that
    // cannot fit the bounded source block, even if the Shell exposes it.
    if (FAILED(array->GetCount(&total)) || !total || total > payloadBytes / sizeof(UINT)) return false;
    std::size_t decodedBytes = 0, found = 0;
    PathMemoryBudget budget;
    const auto consumePath = [&](const wchar_t* path, std::size_t& length) {
        length = 0;
        while (length <= kMaxPath && path[length]) ++length;
        const auto bytes = (length + 1) * sizeof(wchar_t);
        if (length > kMaxPath || bytes > kMaxPayload - decodedBytes) return false;
        decodedBytes += bytes;
        return true;
    };
    for (DWORD i = 0; i < total; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(array->GetItemAt(i, &item)) || !item) return false;
        struct ReleaseItem { IShellItem* value; ~ReleaseItem() { value->Release(); } } releaseItem{item};
        PWSTR path = nullptr;
        const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        struct FreePath { PWSTR value; ~FreePath() { CoTaskMemFree(value); } } freePath{path};
        if (FAILED(result) || !path) {
            // Virtual shell items have no local file; some targets accept them as shortcuts.
            if (!locations) continue;
            PWSTR parsing = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) || !parsing) continue;
            struct FreeParsing { PWSTR value; ~FreeParsing() { CoTaskMemFree(value); } } freeParsing{parsing};
            std::size_t length = 0;
            if (!consumePath(parsing, length)) return false;
            ShellShortcut shortcut;
            if (!shortcut.Resolve(parsing)) continue;
            // Preview the same output path on Enter and Drop. Only Drop writes
            // the shortcut, after its UTF-8 storage has passed the shared budget.
            if (!Utf8Path(shortcut.file.c_str(), shortcut.file.size(), nullptr, &budget)) return false;
            if (paths) {
                if (!shortcut.Save()) continue;
                if (!Utf8Path(shortcut.file.c_str(), shortcut.file.size(), paths)) return false;
            }
            ++found;
            continue;
        }
        std::size_t length = 0;
        if (!consumePath(path, length) || !Utf8Path(path, length, paths, &budget)) return false;
        ++found;
    }
    if (!found) return false;
    count = found;
    return true;
}

bool ReadPayload(IDataObject* data, std::size_t& count, std::vector<std::string>* paths = nullptr, bool locations = false) {
    count = 0;
    if (!data) return false;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (data->QueryGetData(&format) != S_OK) return ReadShellPayload(data, count, paths, locations);
    Medium medium;
    if (FAILED(data->GetData(&format, &medium.value)) || medium.value.tymed != TYMED_HGLOBAL ||
        !medium.value.hGlobal) return false;
    const auto bytes = GlobalSize(medium.value.hGlobal);
    if (bytes < sizeof(DROPFILES) || bytes > kMaxPayload) return false;
    GlobalLockGuard locked(medium.value.hGlobal);
    if (!locked.data) return false;
    DROPFILES header{};
    std::memcpy(&header, locked.data, sizeof(header));
    if (header.pFiles < sizeof(header) || header.pFiles >= bytes) return false;
    const auto* start = static_cast<const unsigned char*>(locked.data) + header.pFiles;
    const auto remaining = bytes - header.pFiles;
    // DROPFILES 是外部内存，不能用无边界的字符串扫描或直接信任 DragQueryFile。
    if (header.fWide) {
        if (header.pFiles % alignof(wchar_t)) return false;
        return ReadList(reinterpret_cast<const wchar_t*>(start), remaining / sizeof(wchar_t), count, paths);
    }
    return ReadList(reinterpret_cast<const char*>(start), remaining, count, paths);
}

struct Callbacks final {
    OrganizerDropTarget::Feedback feedback;
    OrganizerDropTarget::Drop drop;
    bool shellLocations = false;
};

class DropTarget final : public IDropTarget {
public:
    DropTarget(HWND window, std::shared_ptr<OleLease> ole, std::shared_ptr<Callbacks> callbacks)
        : window_(window), ole_(std::move(ole)), callbacks_(std::move(callbacks)) {}

    void EnsureHelper() {
        if (helperAttempted_ || !window_) return;
        helperAttempted_ = true;
        // Shell drag images are optional and need not delay opening a collection.
        // COM activation can pump messages, so keep ownership local until it returns.
        IDropTargetHelper* helper = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
            IID_IDropTargetHelper, reinterpret_cast<void**>(&helper)))) {
            std::shared_ptr<IDropTargetHelper> created(helper, [](IDropTargetHelper* value) { if (value) value->Release(); });
            if (window_) helper_ = std::move(created);
        }
    }

    HRESULT Register() {
        const auto hr = RegisterDragDrop(window_, this);
        registered_ = SUCCEEDED(hr);
        return hr;
    }

    void Detach() noexcept {
        // 先断开可重入状态，再撤销注册和清提示；外部 COM 引用可继续安全存活。
        const auto ole = ole_;
        const auto window = std::exchange(window_, nullptr);
        const bool wasRegistered = std::exchange(registered_, false);
        if (wasRegistered) RevokeDragDrop(window);
        EndDrag();
        callbacks_.reset();
        helper_.reset();
        ole_.reset();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget) return E_NOINTERFACE;
        *result = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL screen, DWORD* effect) override {
        CallGuard guard(*this);
        if (!effect) return E_POINTER;
        const DWORD allowed = std::exchange(*effect, DROPEFFECT_NONE);
        EndDrag();
        if (!window_) return S_OK;
        hovering_ = true;
        try {
            const bool positioned = ClientPixels(window_, screen, point_);
            valid_ = ReadPayload(data, count_, nullptr, callbacks_ && callbacks_->shellLocations) && positioned;
            DWORD selected = valid_ ? AllowedEffect(allowed) : DROPEFFECT_NONE;
            if (!Notify(OrganizerDropFeedback::Phase::Enter, selected != DROPEFFECT_NONE)) {
                valid_ = false;
                selected = DROPEFFECT_NONE;
            }
            if (!window_) return S_OK;
            if (data) EnsureHelper();
            if (!window_) return S_OK;
            const auto helper = helper_;
            if (helper && data) {
                POINT position{screen.x, screen.y};
                imageEntered_ = true;
                try { helper->DragEnter(window_, data, &position, selected); } catch (...) {}
            }
            if (window_) *effect = selected;
        } catch (...) {
            valid_ = false;
            count_ = 0;
            Notify(OrganizerDropFeedback::Phase::Enter, false);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL screen, DWORD* effect) override {
        CallGuard guard(*this);
        if (!effect) return E_POINTER;
        const DWORD allowed = std::exchange(*effect, DROPEFFECT_NONE);
        if (!window_ || !hovering_) return S_OK;
        if (!ClientPixels(window_, screen, point_)) valid_ = false;
        DWORD selected = valid_ ? AllowedEffect(allowed) : DROPEFFECT_NONE;
        if (!Notify(OrganizerDropFeedback::Phase::Over, selected != DROPEFFECT_NONE)) {
            valid_ = false;
            selected = DROPEFFECT_NONE;
        }
        if (!window_) return S_OK;
        const auto helper = helper_;
        if (helper && imageEntered_) {
            POINT position{screen.x, screen.y};
            try { helper->DragOver(&position, selected); } catch (...) {}
        }
        if (window_) *effect = selected;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        CallGuard guard(*this);
        EndDrag();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL screen, DWORD* effect) override {
        CallGuard guard(*this);
        if (!effect) { EndDrag(); return E_POINTER; }
        const DWORD allowed = std::exchange(*effect, DROPEFFECT_NONE);
        if (!window_) return S_OK;
        DWORD selected = DROPEFFECT_NONE;
        try {
            std::vector<std::string> paths;
            std::size_t count = 0;
            const auto candidate = AllowedEffect(allowed);
            if (candidate && ClientPixels(window_, screen, point_) && ReadPayload(data, count, &paths, callbacks_ && callbacks_->shellLocations)) {
                const auto callbacks = callbacks_;
                // 回调可以销毁适配器；成功导入仍返回原本许可的 LINK/COPY。
                if (window_ && callbacks && callbacks->drop && callbacks->drop(paths, point_)) selected = candidate;
            }
        } catch (...) { selected = DROPEFFECT_NONE; }
        const auto helper = helper_;
        if (helper && std::exchange(imageEntered_, false)) {
            POINT position{screen.x, screen.y};
            try { helper->Drop(data, &position, selected); } catch (...) {}
        }
        EndDrag();
        *effect = selected;
        return S_OK;
    }

private:
    // 回调可能 Detach/销毁 owner，调用期间同时保住对象和当前线程的 OLE lease。
    struct CallGuard final {
        explicit CallGuard(DropTarget& value) : target(value), ole(value.ole_) { target.AddRef(); }
        ~CallGuard() { target.Release(); }
        DropTarget& target;
        std::shared_ptr<OleLease> ole;
    };
    ~DropTarget() = default;

    bool Notify(OrganizerDropFeedback::Phase phase, bool supported) noexcept {
        const auto callbacks = callbacks_;
        if (!callbacks || !callbacks->feedback) return true;
        try { callbacks->feedback({phase, point_, count_, supported}); return true; }
        catch (...) { return false; }
    }

    void EndDrag() noexcept {
        const bool wasHovering = std::exchange(hovering_, false);
        const bool hadImage = std::exchange(imageEntered_, false);
        valid_ = false;
        count_ = 0;
        const auto helper = helper_;
        if (helper && hadImage) {
            try { helper->DragLeave(); } catch (...) {}
        }
        if (wasHovering) Notify(OrganizerDropFeedback::Phase::Leave, false);
    }

    std::atomic<ULONG> references_{1};
    HWND window_ = nullptr;
    std::shared_ptr<OleLease> ole_;
    std::shared_ptr<Callbacks> callbacks_;
    std::shared_ptr<IDropTargetHelper> helper_;
    bool helperAttempted_ = false;
    bool registered_ = false, hovering_ = false, valid_ = false, imageEntered_ = false;
    std::size_t count_ = 0;
    POINT point_{};
};
}

struct OrganizerDropTarget::Impl final {
    IDropTarget* target = nullptr;
    ~Impl() {
        if (target) { static_cast<DropTarget*>(target)->Detach(); target->Release(); }
    }
};
OrganizerDropTarget::OrganizerDropTarget() = default;
OrganizerDropTarget::~OrganizerDropTarget() { Detach(); }

bool OrganizerDropTarget::Attach(HWND window, Feedback feedback, Drop drop, bool shellLocations) {
    if (!window || !drop || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    Detach();
    if (impl_) return false;
    try {
        auto ole = std::make_shared<OleLease>();
        if (FAILED(ole->result)) return false;
        auto callbacks = std::make_shared<Callbacks>(Callbacks{std::move(feedback), std::move(drop), shellLocations});
        auto impl = std::make_unique<Impl>();
        impl->target = new DropTarget(window, std::move(ole), std::move(callbacks));
        if (FAILED(static_cast<DropTarget*>(impl->target)->Register())) return false;
        impl_ = std::move(impl);
        return true;
    } catch (...) { return false; }
}

void OrganizerDropTarget::Detach() {
    auto detached = std::move(impl_);
    detached.reset();
}

namespace {
bool ShellShortcut::Resolve(const std::wstring& parsingName) {
    if (parsingName.empty() || parsingName.size() > 1024) return false;
    if (FAILED(SHParseDisplayName(parsingName.c_str(), nullptr, &id, 0, nullptr)) || !id) return false;
    PWSTR display = nullptr;
    std::wstring name;
    if (SUCCEEDED(SHGetNameFromIDList(id, SIGDN_NORMALDISPLAY, &display)) && display) { name = display; CoTaskMemFree(display); }
    for (auto& ch : name) if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) ch = L'_';
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    if (name.empty() || name.size() > 80) return false;
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) || !local) return false;
    folder = std::wstring(local) + L"\\Lume\\shell-items";
    CoTaskMemFree(local);
    file = folder + L"\\" + name + L".lnk";
    return true;
}

bool ShellShortcut::Save() const {
    const int created = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS) return false;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) || !link) return false;
    struct ReleaseLink { IShellLinkW* value; ~ReleaseLink() { value->Release(); } } releaseLink{link};
    IPersistFile* persist = nullptr;
    if (FAILED(link->SetIDList(id)) || FAILED(link->QueryInterface(IID_PPV_ARGS(&persist))) || !persist) return false;
    const HRESULT saved = persist->Save(file.c_str(), TRUE);
    persist->Release();
    return SUCCEEDED(saved);
}
}

std::optional<std::string> ShellLocationShortcut(const std::wstring& parsingName) {
    ShellShortcut shortcut;
    if (!shortcut.Resolve(parsingName) || !shortcut.Save()) return std::nullopt;
    std::vector<std::string> out;
    if (!Utf8Path(shortcut.file.c_str(), shortcut.file.size(), &out) || out.empty()) return std::nullopt;
    return out.front();
}
}
