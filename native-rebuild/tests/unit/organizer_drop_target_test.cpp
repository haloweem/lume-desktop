#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct ComRelease {
    void operator()(T* value) const { if (value) value->Release(); }
};
template<class T> using ComPtr = std::unique_ptr<T, ComRelease<T>>;
struct HelperEvent { char phase; POINT point; DWORD effect; };
struct HelperTrace { std::vector<HelperEvent> events; unsigned destroyed = 0; };
std::shared_ptr<HelperTrace> helperTrace;
bool helperUnavailable = false;
bool realHelper = false;
unsigned helperCreations = 0;
std::function<void()> helperCreating;
thread_local int oleBalance = 0;
std::map<HWND, IDropTarget*> registeredTargets;

class Helper final : public IDropTargetHelper {
public:
    explicit Helper(std::shared_ptr<HelperTrace> trace) : trace_(std::move(trace)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTargetHelper) return E_NOINTERFACE;
        *out = static_cast<IDropTargetHelper*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = --refs_; if (!refs) delete this; return refs;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(HWND, IDataObject*, POINT* p, DWORD effect) override {
        trace_->events.push_back({'E', *p, effect}); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        trace_->events.push_back({'L', {}, 0}); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(POINT* p, DWORD effect) override {
        trace_->events.push_back({'O', *p, effect}); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, POINT* p, DWORD effect) override {
        trace_->events.push_back({'D', *p, effect}); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Show(BOOL) override { return S_OK; }
private:
    ~Helper() { ++trace_->destroyed; }
    ULONG refs_ = 1;
    std::shared_ptr<HelperTrace> trace_;
};

HRESULT WINAPI TestOleInitialize(void* reserved) {
    const auto hr = ::OleInitialize(reserved);
    if (SUCCEEDED(hr)) ++oleBalance;
    return hr;
}
void WINAPI TestOleUninitialize() { --oleBalance; ::OleUninitialize(); }
HRESULT WINAPI TestRegisterDragDrop(HWND window, IDropTarget* target) {
    const auto hr = ::RegisterDragDrop(window, target);
    if (SUCCEEDED(hr)) registeredTargets.emplace(window, target);
    return hr;
}
HRESULT WINAPI TestRevokeDragDrop(HWND window) {
    const auto hr = ::RevokeDragDrop(window);
    registeredTargets.erase(window);
    return hr;
}
HRESULT WINAPI TestCoCreateInstance(REFCLSID clsid, IUnknown* outer, DWORD context,
                                   REFIID iid, void** out) {
    if (clsid != CLSID_DragDropHelper || realHelper)
        return ::CoCreateInstance(clsid, outer, context, iid, out);
    if (!out) return E_POINTER;
    *out = nullptr;
    ++helperCreations;
    if (helperCreating) helperCreating();
    if (helperUnavailable) return REGDB_E_CLASSNOTREG;
    ComPtr<Helper> helper(new Helper(helperTrace));
    return helper->QueryInterface(iid, out);
}
}

// 仅在测试翻译单元包装公开 Windows API；注册与撤销仍调用真实 OLE。
// 包含实现以取得注册的接口并验证 helper/初始化配对，不增加生产测试接口。
#define OleInitialize TestOleInitialize
#define OleUninitialize TestOleUninitialize
#define RegisterDragDrop TestRegisterDragDrop
#define RevokeDragDrop TestRevokeDragDrop
#define CoCreateInstance TestCoCreateInstance
#include "platform/windows/organizer/organizer_drop_target.cpp"
#undef CoCreateInstance
#undef RevokeDragDrop
#undef RegisterDragDrop
#undef OleUninitialize
#undef OleInitialize

namespace {
using lume::platform::windows::OrganizerDropFeedback;
using lume::platform::windows::OrganizerDropTarget;
using Phase = OrganizerDropFeedback::Phase;

struct Window {
    HWND handle = CreateWindowExW(WS_EX_ACCEPTFILES, L"STATIC", L"OLE isolated fixture",
        WS_OVERLAPPEDWINDOW, 173, 211, 300, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Window() { Check(handle != nullptr, "create hidden window"); }
    ~Window() { DestroyWindow(handle); }
    POINTL Screen(POINT client) const {
        const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        POINT origin{};
        Check(ClientToScreen(handle, &origin) != FALSE, "client origin to physical screen");
        SetThreadDpiAwarenessContext(previous);
        return {origin.x + client.x, origin.y + client.y};
    }
};
ComPtr<IDropTarget> Target(HWND window) {
    const auto at = registeredTargets.find(window);
    Check(at != registeredTargets.end(), "Attach must register a real OLE drop target");
    at->second->AddRef();
    return ComPtr<IDropTarget>(at->second);
}

std::vector<unsigned char> WidePayload(const std::vector<std::wstring>& paths) {
    std::size_t units = paths.empty() ? 2 : 1;
    for (const auto& path : paths) units += path.size() + 1;
    std::vector<unsigned char> bytes(sizeof(DROPFILES) + units * sizeof(wchar_t), 0);
    DROPFILES header{}; header.pFiles = sizeof(DROPFILES); header.fWide = TRUE;
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::size_t offset = sizeof(DROPFILES);
    for (const auto& path : paths) {
        std::memcpy(bytes.data() + offset, path.data(), path.size() * sizeof(wchar_t));
        offset += (path.size() + 1) * sizeof(wchar_t);
    }
    return bytes;
}

struct MediumStats { int created = 0, released = 0; };
class MediumOwner final : public IUnknown {
public:
    MediumOwner(HGLOBAL memory, MediumStats& stats) : memory_(memory), stats_(stats) { ++stats_.created; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown) return E_NOINTERFACE;
        *out = static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_; if (!refs) delete this; return refs;
    }
private:
    ~MediumOwner() { GlobalFree(memory_); ++stats_.released; }
    ULONG refs_ = 1;
    HGLOBAL memory_;
    MediumStats& stats_;
};

class Data final : public IDataObject {
public:
    explicit Data(std::vector<std::wstring> paths) : bytes(WidePayload(paths)) {}
    std::vector<unsigned char> bytes;
    unsigned gets = 0, queries = 0;
    ULONG refs = 1;
    MediumStats media;
    HRESULT queryResult = S_OK, getResult = S_OK;
    bool wrongMedium = false;
    bool throwGet = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        ++gets;
        if (throwGet) throw std::runtime_error("data failure");
        if (FAILED(getResult)) return getResult;
        if (!Valid(format) || !medium) return DV_E_FORMATETC;
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (!memory) return E_OUTOFMEMORY;
        auto* destination = GlobalLock(memory);
        if (!destination) { GlobalFree(memory); return E_OUTOFMEMORY; }
        std::memset(destination, 0x7f, GlobalSize(memory));
        std::memcpy(destination, bytes.data(), bytes.size()); GlobalUnlock(memory);
        *medium = {};
        medium->tymed = wrongMedium ? TYMED_NULL : TYMED_HGLOBAL;
        medium->hGlobal = memory;
        medium->pUnkForRelease = new MediumOwner(memory, media);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        ++queries; return Valid(format) ? queryResult : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    static bool Valid(const FORMATETC* f) {
        return f && f->cfFormat == CF_HDROP && f->dwAspect == DVASPECT_CONTENT &&
            f->lindex == -1 && f->tymed == TYMED_HGLOBAL && f->ptd == nullptr;
    }
};

struct Fixture {
    Window window;
    std::vector<OrganizerDropFeedback> feedback;
    std::vector<std::string> dropped;
    POINT dropPoint{};
    unsigned drops = 0;
    bool accept = true;
    OrganizerDropTarget adapter;
    ComPtr<IDropTarget> target;
    Fixture() {
        Check(adapter.Attach(window.handle, [this](const auto& event) { feedback.push_back(event); },
            [this](const auto& paths, POINT point) {
                ++drops; dropped = paths; dropPoint = point; return accept;
            }), "Attach hidden window");
        target = Target(window.handle);
    }
    ~Fixture() { adapter.Detach(); }
    DWORD Enter(Data& data, DWORD allowed = DROPEFFECT_LINK | DROPEFFECT_COPY | DROPEFFECT_MOVE) {
        Check(SUCCEEDED(target->DragEnter(&data, 0, window.Screen({23, 41}), &allowed)), "DragEnter HRESULT");
        return allowed;
    }
    DWORD DropData(Data& data, DWORD allowed = DROPEFFECT_LINK | DROPEFFECT_COPY) {
        Check(SUCCEEDED(target->Drop(&data, 0, window.Screen({31, 47}), &allowed)), "Drop HRESULT");
        return allowed;
    }
};

class ShellOnlyData final : public IDataObject {
public:
    explicit ShellOnlyData(IDataObject* source):source_(source){}
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_IDataObject)return E_NOINTERFACE;
        *out=static_cast<IDataObject*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{return f&&f->cfFormat==CF_HDROP?DV_E_FORMATETC:source_->QueryGetData(f);}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM* m)override{return f&&f->cfFormat==CF_HDROP?DV_E_FORMATETC:source_->GetData(f,m);}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* f,STGMEDIUM* m)override{return source_->GetDataHere(f,m);}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* f,FORMATETC* out)override{return source_->GetCanonicalFormatEtc(f,out);}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* f,STGMEDIUM* m,BOOL release)override{return source_->SetData(f,m,release);}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** out)override{return source_->EnumFormatEtc(direction,out);}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* f,DWORD flags,IAdviseSink* sink,DWORD* connection)override{return source_->DAdvise(f,flags,sink,connection);}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection)override{return source_->DUnadvise(connection);}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** out)override{return source_->EnumDAdvise(out);}
private:IDataObject* source_;
};

void MixedDesktopSelection(){
    Fixture fixture;
    const auto directory=std::filesystem::temp_directory_path()/(L"lume-drop-selection-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    struct Cleanup{std::filesystem::path directory;std::vector<PIDLIST_ABSOLUTE> ids;
        ~Cleanup(){for(auto id:ids)CoTaskMemFree(id);std::error_code error;std::filesystem::remove_all(directory,error);}} cleanup{directory,{}};
    PIDLIST_ABSOLUTE desktop=nullptr;
    Check(SUCCEEDED(SHGetSpecialFolderLocation(nullptr,CSIDL_DESKTOP,&desktop)),"create desktop namespace root");
    cleanup.ids.push_back(desktop);
    PIDLIST_ABSOLUTE recycle=nullptr;
    Check(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder,0,nullptr,&recycle)),"get recycle bin namespace item");
    cleanup.ids.push_back(recycle);
    std::vector<PCUITEMID_CHILD> children{reinterpret_cast<PCUITEMID_CHILD>(recycle)};
    for(unsigned i=0;i<24;++i){
        const auto path=directory/(L"batch-"+std::to_wstring(i)+L".txt");std::ofstream(path)<<"fixture";
        PIDLIST_ABSOLUTE id=nullptr;Check(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&id,0,nullptr)),"parse batch fixture path");
        cleanup.ids.push_back(id);children.push_back(reinterpret_cast<PCUITEMID_CHILD>(id));
    }
    IDataObject* raw=nullptr;
    Check(SUCCEEDED(SHCreateDataObject(desktop,static_cast<UINT>(children.size()),children.data(),nullptr,IID_PPV_ARGS(&raw))),
          "create genuine multi-item desktop Shell data object");
    ComPtr<IDataObject> native(raw);ShellOnlyData data(native.get());
    std::size_t nativeCount=0;std::vector<std::string> nativePaths;
    Check(lume::platform::windows::ReadPayload(native.get(),nativeCount,&nativePaths)&&nativeCount==24&&nativePaths.size()==24,
          "genuine mixed desktop object yields every local path without a format wrapper");
    // The Dock accepts shell locations as shortcuts that point at the location itself.
    std::size_t withLocations=0;std::vector<std::string> locationPaths;
    Check(lume::platform::windows::ReadPayload(&data,withLocations,&locationPaths,true)&&withLocations==25&&locationPaths.size()==25,
          "shell locations are accepted when the target asks for them");
    const auto link=std::find_if(locationPaths.begin(),locationPaths.end(),[](const std::string& p){return p.size()>4&&p.substr(p.size()-4)==".lnk";});
    Check(link!=locationPaths.end()&&std::filesystem::exists(std::filesystem::u8path(*link)),"recycle bin becomes a real shortcut file");
    {   IShellLinkW* shell=nullptr;IPersistFile* file=nullptr;PIDLIST_ABSOLUTE target=nullptr;
        Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shell)))&&
              SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&file)))&&SUCCEEDED(file->Load(std::filesystem::u8path(*link).c_str(),STGM_READ))&&
              SUCCEEDED(shell->GetIDList(&target))&&target&&ILIsEqual(target,recycle),"the shortcut points at the recycle bin");
        CoTaskMemFree(target);file->Release();shell->Release();}
    DWORD effect=DROPEFFECT_COPY|DROPEFFECT_LINK;
    Check(SUCCEEDED(fixture.target->DragEnter(&data,0,fixture.window.Screen({23,41}),&effect))&&effect!=DROPEFFECT_NONE,
          "mixed recycle-bin and ordinary files no longer show a prohibited drop");
    Check(fixture.feedback.back().count==24,"feedback counts all importable ordinary files");
    effect=DROPEFFECT_COPY|DROPEFFECT_LINK;
    Check(SUCCEEDED(fixture.target->Drop(&data,0,fixture.window.Screen({31,47}),&effect))&&effect!=DROPEFFECT_NONE&&fixture.dropped.size()==24,
          "one drop imports all 24 ordinary paths while skipping the virtual recycle-bin item");
    for(const auto& path:fixture.dropped)Check(std::filesystem::exists(std::filesystem::u8path(path)),"batch paths remain unchanged on disk");
}

void MultiPathAndCache() {
    Fixture f;
    Data data({L"Z:\\never-created\\one.txt", L"\\\\invalid.example\\offline\\\u4e8c.lnk"});
    Check(f.Enter(data) == DROPEFFECT_LINK, "prefer LINK");
    Check(data.gets == 1 && data.queries == 1 && data.media.released == 1, "Enter reads and releases once");
    Check(data.refs == 1, "Enter must not retain data object");
    Check(f.feedback.back().phase == Phase::Enter && f.feedback.back().count == 2 &&
        f.feedback.back().supported, "Enter feedback for multiple paths");
    Check(f.feedback.back().point.x == 23 && f.feedback.back().point.y == 41, "physical client Enter point");
    for (unsigned i = 0; i < 100; ++i) {
        DWORD effect = DROPEFFECT_COPY;
        Check(SUCCEEDED(f.target->DragOver(MK_SHIFT, f.window.Screen({29, 43}), &effect)) &&
            effect == DROPEFFECT_COPY, "Over uses current source allowance, never SHIFT move");
    }
    Check(data.gets == 1 && data.queries == 1, "Over performs no data reads");
    Check(f.feedback.back().phase == Phase::Over && f.feedback.back().count == 2, "cached Over count");
    Check(f.DropData(data) == DROPEFFECT_LINK, "successful Drop LINK");
    Check(f.drops == 1 && f.dropped.size() == 2 &&
        f.dropped[1] == "\\\\invalid.example\\offline\\\xe4\xba\x8c.lnk", "all paths arrive as UTF-8 without disk access");
    Check(f.dropPoint.x == 31 && f.dropPoint.y == 47, "physical client Drop point");
    Check(data.gets == 2 && data.media.created == data.media.released, "Drop revalidates and releases medium");
    Check(f.feedback.back().phase == Phase::Leave && !f.feedback.back().supported &&
        f.feedback.back().count == 0, "Drop clears feedback");
    Check(helperTrace->events.front().phase == 'E' && helperTrace->events.back().phase == 'D', "native helper Enter/Drop");
    const auto screen = f.window.Screen({23, 41});
    Check(helperTrace->events.front().point.x == screen.x &&
        helperTrace->events.front().point.y == screen.y, "helper uses screen coordinates");
    Check(helperTrace->events.back().effect == DROPEFFECT_LINK, "helper receives final successful effect");
}

void EffectsAndFailure() {
    Fixture f; Data data({L"Q:\\fictional.txt"});
    for (DWORD allowed : {DWORD(DROPEFFECT_NONE), DWORD(DROPEFFECT_MOVE), DWORD(DROPEFFECT_SCROLL)}) {
        Check(f.Enter(data, allowed) == DROPEFFECT_NONE, "unsupported source effects return NONE");
        Check(!f.feedback.back().supported, "MOVE-only is not supported");
        Check(f.DropData(data, allowed) == DROPEFFECT_NONE && !f.drops, "forbidden effect never imports");
    }
    Check(f.Enter(data, DROPEFFECT_COPY | DROPEFFECT_MOVE) == DROPEFFECT_COPY, "COPY fallback");
    Check(f.DropData(data, DROPEFFECT_COPY | DROPEFFECT_MOVE) == DROPEFFECT_COPY, "Drop COPY fallback");
    f.accept = false; f.Enter(data);
    Check(f.DropData(data) == DROPEFFECT_NONE, "import rejection returns NONE");
    Check(helperTrace->events.back().effect == DROPEFFECT_NONE, "helper sees import rejection");
}

void RevalidatePayload() {
    Fixture f; Data data({L"Q:\\initial.txt"}); f.Enter(data);
    data.bytes = WidePayload({L"Q:\\new-a.txt", L"Q:\\new-b.txt"});
    Check(f.DropData(data) == DROPEFFECT_LINK && f.dropped.size() == 2 &&
        f.dropped.front() == "Q:\\new-a.txt", "Drop imports current payload");
    f.Enter(data); data.getResult = DV_E_FORMATETC;
    Check(f.DropData(data) == DROPEFFECT_NONE && f.drops == 1, "failed Drop read rejects cached payload");
    data.getResult = S_OK; f.Enter(data); data.queryResult = DV_E_FORMATETC;
    Check(f.DropData(data) == DROPEFFECT_NONE && f.drops == 1, "Drop rechecks advertised format");
}

void Limits() {
    Fixture f;
    Data data(std::vector<std::wstring>(1024, L"Q:\\not-present.txt"));
    Check(f.Enter(data) == DROPEFFECT_LINK && f.feedback.back().count == 1024, "1024 paths accepted");
    Check(f.DropData(data) == DROPEFFECT_LINK && f.dropped.size() == 1024, "1024 paths imported");
    data.bytes = WidePayload(std::vector<std::wstring>(1025, L"Q:\\not-present.txt"));
    Check(f.Enter(data) == DROPEFFECT_NONE && f.DropData(data) == DROPEFFECT_NONE, "1025 paths rejected");
    data.bytes = WidePayload({std::wstring(32767, L'x')});
    Check(f.Enter(data) == DROPEFFECT_LINK && f.DropData(data) == DROPEFFECT_LINK &&
        f.dropped.front().size() == 32767, "32767 UTF-16 units accepted");
    data.bytes = WidePayload({std::wstring(32768, L'x')});
    Check(f.Enter(data) == DROPEFFECT_NONE && f.DropData(data) == DROPEFFECT_NONE, "32768 UTF-16 units rejected");
    Check(data.media.created == data.media.released, "limits release every medium");
}

void MalformedPayloads() {
    Fixture f; Data data({L"Q:\\valid.txt"});
    std::vector<std::vector<unsigned char>> cases;
    cases.push_back(WidePayload({}));
    cases.push_back(std::vector<unsigned char>(sizeof(DROPFILES) - 1, 0x7f));
    auto invalidOffset = WidePayload({L"x"});
    DROPFILES header{}; std::memcpy(&header, invalidOffset.data(), sizeof(header));
    header.pFiles = 0xfffffff0u; std::memcpy(invalidOffset.data(), &header, sizeof(header));
    cases.push_back(invalidOffset);
    header.pFiles = 0; std::memcpy(invalidOffset.data(), &header, sizeof(header)); cases.push_back(invalidOffset);
    header.pFiles = sizeof(DROPFILES) + 1; std::memcpy(invalidOffset.data(), &header, sizeof(header)); cases.push_back(invalidOffset);
    auto unterminated = WidePayload({L"abc"});
    std::fill(unterminated.begin() + sizeof(DROPFILES), unterminated.end(), static_cast<unsigned char>(0x41));
    cases.push_back(unterminated);
    auto missingFinal = WidePayload({L"abc"}); missingFinal.resize(missingFinal.size() - sizeof(wchar_t));
    cases.push_back(missingFinal);
    cases.push_back(WidePayload({std::wstring(1, wchar_t(0xd800))}));
    for (const auto& bytes : cases) {
        data.bytes = bytes;
        Check(f.Enter(data) == DROPEFFECT_NONE, "malformed Enter rejected");
        Check(f.feedback.back().point.x == 23 && f.feedback.back().point.y == 41, "unsupported feedback still uses physical client point");
        Check(!f.feedback.back().supported && f.feedback.back().count == 0, "no partial count on malformed data");
        Check(f.DropData(data) == DROPEFFECT_NONE && !f.drops, "malformed Drop never imports");
    }
    data.bytes = WidePayload({L"Q:\\valid.txt"}); data.wrongMedium = true;
    Check(f.Enter(data) == DROPEFFECT_NONE, "unexpected medium rejected");
    Check(data.media.created == data.media.released, "malformed medium released using pUnkForRelease");
    data.wrongMedium = false; data.queryResult = DV_E_FORMATETC;
    const auto before = data.gets;
    Check(f.Enter(data) == DROPEFFECT_NONE && data.gets == before, "unsupported format is not retrieved");
}

void AnsiPayload() {
    Fixture f; Data data({});
    const char names[] = "Q:\\one.txt\0Q:\\two.lnk\0";
    data.bytes.assign(sizeof(DROPFILES) + sizeof(names), 0);
    DROPFILES header{}; header.pFiles = sizeof(header);
    std::memcpy(data.bytes.data(), &header, sizeof(header));
    std::memcpy(data.bytes.data() + sizeof(header), names, sizeof(names));
    Check(f.Enter(data) == DROPEFFECT_LINK && f.DropData(data) == DROPEFFECT_LINK &&
        f.dropped == std::vector<std::string>({"Q:\\one.txt", "Q:\\two.lnk"}), "ANSI HDROP decoded");
}

void LeaveAndDetach() {
    Fixture f; Data data({L"Q:\\a.txt"}); f.Enter(data);
    Check(SUCCEEDED(f.target->DragLeave()), "DragLeave HRESULT");
    Check(f.feedback.back().phase == Phase::Leave && !f.feedback.back().supported, "Leave clears hint");
    DWORD effect = DROPEFFECT_LINK;
    Check(SUCCEEDED(f.target->DragOver(0, {}, &effect)) && !effect, "Over after Leave cannot reuse payload");
    f.Enter(data); f.adapter.Detach();
    Check(f.feedback.back().phase == Phase::Leave && !registeredTargets.count(f.window.handle), "Detach clears and revokes");
    Check(helperTrace->events.back().phase == 'L' && helperTrace->destroyed == 1, "Detach ends native drag image");
    const auto events = f.feedback.size();
    effect = DROPEFFECT_LINK;
    Check(SUCCEEDED(f.target->DragEnter(&data, 0, {}, &effect)) && !effect, "retained COM interface inert after Detach");
    Check(f.DropData(data) == DROPEFFECT_NONE && f.feedback.size() == events && !f.drops, "detached interface cannot call owner");
    f.adapter.Detach(); Check(oleBalance == 0, "Detach balances OLE before final external Release");
    Check((GetWindowLongPtrW(f.window.handle, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0, "WM_DROPFILES fallback flag preserved");
}

void RegistrationAndIdentity() {
    Window window; OrganizerDropTarget first, second;
    Check(!first.Attach(nullptr, {}, [](const auto&, POINT) { return true; }), "null HWND rejected");
    Check(!first.Attach(window.handle, {}, {}), "missing import callback rejected");
    Check(first.Attach(window.handle, {}, [](const auto&, POINT) { return true; }), "valid registration");
    auto target = Target(window.handle);
    void* identity = nullptr;
    Check(target->QueryInterface(IID_IUnknown, &identity) == S_OK && identity, "IUnknown available");
    static_cast<IUnknown*>(identity)->Release();
    identity = reinterpret_cast<void*>(1);
    Check(target->QueryInterface(IID_IDataObject, &identity) == E_NOINTERFACE && !identity, "unknown IID resets output");
    Check(target->QueryInterface(IID_IUnknown, nullptr) == E_POINTER, "QI null output");
    Check(target->DragEnter(nullptr, 0, {}, nullptr) == E_POINTER, "null effect pointer");
    DWORD effect = DROPEFFECT_LINK;
    target->DragEnter(nullptr, 0, {}, &effect); Check(!effect, "null data object rejected");
    const int balance = oleBalance;
    Check(!second.Attach(window.handle, {}, [](const auto&, POINT) { return true; }), "duplicate registration rejected");
    Check(oleBalance == balance && registeredTargets.count(window.handle), "failed registration balances own init and preserves first");
    first.Detach();
    Check(second.Attach(window.handle, {}, [](const auto&, POINT) { return true; }), "window can register after revoke");
    second.Detach(); Check(oleBalance == 0, "all registration init balanced");
}

void CallbackExceptions() {
    Window window; OrganizerDropTarget adapter; Data data({L"Q:\\a.txt"});
    Check(adapter.Attach(window.handle, [](const auto&) { throw std::runtime_error("feedback"); },
        [](const auto&, POINT) { return true; }), "attach throwing feedback");
    auto target = Target(window.handle); DWORD effect = DROPEFFECT_LINK;
    target->DragEnter(&data, 0, {}, &effect); Check(!effect, "feedback exception contained with NONE");
    target->DragLeave(); adapter.Detach();
    Check(adapter.Attach(window.handle, {}, [](const auto&, POINT) -> bool { throw std::runtime_error("drop"); }), "attach throwing import");
    target = Target(window.handle); effect = DROPEFFECT_LINK;
    target->DragEnter(&data, 0, {}, &effect);
    effect = DROPEFFECT_LINK; target->Drop(&data, 0, {}, &effect);
    Check(!effect && data.media.created == data.media.released, "Drop exception contained and medium released");
    data.throwGet = true; effect = DROPEFFECT_LINK;
    target->DragEnter(&data, 0, {}, &effect); Check(!effect, "data object exception contained");
}

void ReentrantDestruction() {
    Window window; Data data({L"Q:\\a.txt"});
    auto adapter = std::make_unique<OrganizerDropTarget>(); unsigned leaves = 0;
    Check(adapter->Attach(window.handle, [&](const auto& event) {
        if (event.phase == Phase::Enter) adapter.reset();
        if (event.phase == Phase::Leave) ++leaves;
    }, [](const auto&, POINT) { return true; }), "attach reentrant feedback");
    auto target = Target(window.handle); DWORD effect = DROPEFFECT_LINK;
    target->DragEnter(&data, 0, {}, &effect);
    Check(!effect && !adapter && leaves == 1 && !oleBalance, "destruction inside feedback clears safely");
    adapter = std::make_unique<OrganizerDropTarget>();
    Check(adapter->Attach(window.handle, {}, [&](const auto&, POINT) { adapter.reset(); return true; }), "attach reentrant import");
    target = Target(window.handle); effect = DROPEFFECT_COPY;
    target->DragEnter(&data, 0, {}, &effect); effect = DROPEFFECT_COPY;
    Check(SUCCEEDED(target->Drop(&data, 0, {}, &effect)) && effect == DROPEFFECT_COPY &&
        !adapter && !oleBalance, "successful import can destroy owner safely");
}

void OleApartments() {
    Check(SUCCEEDED(::OleInitialize(nullptr)), "outer OLE init");
    { Fixture a; Fixture b; a.adapter.Detach(); Check(oleBalance > 0, "second window retains OLE"); }
    Check(!oleBalance, "adapter balances nested S_FALSE initializations");
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == RPC_E_CHANGED_MODE, "outer STA initialization preserved");
    ::OleUninitialize();
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK, "all STA references balanced");
    { Window w; OrganizerDropTarget adapter;
      Check(!adapter.Attach(w.handle, {}, [](const auto&, POINT) { return true; }), "MTA rejects OLE registration"); }
    CoUninitialize(); Check(!oleBalance, "failed OleInitialize has no uninitialize");
    Window mainWindow;
    bool rejected = false, ownThreadWorks = false;
    std::thread worker([&] {
        OrganizerDropTarget adapter;
        rejected = !adapter.Attach(mainWindow.handle, {}, [](const auto&, POINT) { return true; });
        Window own;
        ownThreadWorks = adapter.Attach(own.handle, {}, [](const auto&, POINT) { return true; });
        adapter.Detach(); ownThreadWorks = ownThreadWorks && oleBalance == 0;
    });
    worker.join(); Check(rejected && ownThreadWorks, "registration belongs to window thread with per-thread OLE balance");
}

void DpiAndOptionalHelper() {
    const auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    { Fixture f; Data data({L"Q:\\a.txt"});
      const auto before = GetThreadDpiAwarenessContext(); f.Enter(data);
      Check(f.feedback.back().point.x == 23 && f.feedback.back().point.y == 41, "physical point for unaware HWND");
      Check(AreDpiAwarenessContextsEqual(before, GetThreadDpiAwarenessContext()), "DPI context restored"); }
    SetThreadDpiAwarenessContext(old);
    helperUnavailable = true;
    { Fixture f; Data data({L"Q:\\a.txt"});
      Check(f.Enter(data) == DROPEFFECT_LINK && f.DropData(data) == DROPEFFECT_LINK, "helper absence does not block imports"); }
    helperUnavailable = false;
    realHelper = true;
    { Fixture f; Data data({L"Q:\\a.txt"});
      Check(f.Enter(data) == DROPEFFECT_LINK && f.DropData(data) == DROPEFFECT_LINK, "real Shell helper smoke check"); }
    realHelper = false;
}
}

void LazyDragHelper() {
    helperCreations = 0;
    Fixture fixture;
    Check(helperCreations == 0, "opening organizer must not activate the Shell drag-image helper");
    Data data({L"Z:\\never-created\\one.txt"});
    fixture.Enter(data);
    Check(helperCreations == 1, "first drag activates its image helper");
    fixture.target->DragLeave();
    fixture.Enter(data);
    Check(helperCreations == 1, "subsequent drags reuse the helper");
}
void DetachDuringHelperActivation() {
    Fixture fixture;
    Data data({L"Z:\\never-created\\one.txt"});
    helperCreating = [&] { fixture.adapter.Detach(); };
    const auto effect = fixture.Enter(data);
    helperCreating = {};
    Check(effect == DROPEFFECT_NONE, "COM activation reentrancy cannot revive a detached target");
    Check(helperTrace->destroyed == 1 && helperTrace->events.empty(), "detached target releases the new helper without using it");
}
int main() {
    std::cout << std::unitbuf;
    const std::pair<const char*, void(*)()> tests[] = {
        {"lazy drag-image helper", LazyDragHelper},
        {"mixed desktop selection", MixedDesktopSelection},
        {"detach during helper activation", DetachDuringHelperActivation},
        {"multi-path/cache/helper", MultiPathAndCache}, {"effects/rejected import", EffectsAndFailure},
        {"Drop revalidation", RevalidatePayload}, {"count/path limits", Limits},
        {"malformed/unsupported payload", MalformedPayloads}, {"ANSI payload", AnsiPayload},
        {"Leave/Detach/retained interface", LeaveAndDetach}, {"registration/IUnknown", RegistrationAndIdentity},
        {"COM exception boundaries", CallbackExceptions}, {"reentrant destruction", ReentrantDestruction},
        {"thread/OLE balance", OleApartments}, {"DPI/optional helper", DpiAndOptionalHelper}
    };
    unsigned failed = 0;
    for (const auto& test : tests) {
        helperTrace = std::make_shared<HelperTrace>();
        try {
            test.second();
            Check(registeredTargets.empty() && oleBalance == 0, "fixture must leave no registration or OLE lease");
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& e) {
            ++failed; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
        }
    }
    std::cout << (std::size(tests) - failed) << '/' << std::size(tests) << " passed\n";
    return failed ? 1 : 0;
}
