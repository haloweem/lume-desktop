#include "platform/windows/webview_page_window.h"
#include "platform/windows/webview_com_support.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using lume::platform::windows::WebViewPageWindow;
using lume::platform::windows::webview_detail::ComPtr;
constexpr ULONGLONG kDeadlineMs = 60000;
constexpr unsigned kFileStages = 4;
constexpr UINT_PTR kMotionSubclass = 0x4d4f544e;
constexpr std::array<const char*, 9> kStages{
    "local", "official", "detail", "returned", "blank", "input", "button", "scroll", "scrollbar"};

std::wstring WideJson(const Json& value) {
    const auto ascii = value.dump(-1, ' ', true);
    return {ascii.begin(), ascii.end()};
}

bool MovePointer(POINT point) {
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 1 || height <= 1) return false;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, width - 1);
    input.mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, height - 1);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool MouseButton(DWORD flag) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    return SendInput(1,&input,sizeof(input)) == 1;
}

class TimedDropSource final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IDropSource)) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD) override {
        ++queries;
        if (escape) return DRAGDROP_S_CANCEL;
        return GetTickCount64() - started_ >= 1200 ? DRAGDROP_S_DROP : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override {
        ++feedbacks;
        if (effect & DROPEFFECT_COPY) copyFeedback = true;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
    unsigned queries = 0, feedbacks = 0;
    bool copyFeedback = false;
private:
    std::atomic<ULONG> references_{1};
    ULONGLONG started_ = GetTickCount64();
};

bool CreateFileDataObject(const fs::path& directory, const std::vector<fs::path>& files,
    ComPtr<IDataObject>& object) {
    PIDLIST_ABSOLUTE parent = nullptr;
    if (FAILED(SHParseDisplayName(directory.c_str(), nullptr, &parent, 0, nullptr))) return false;
    std::vector<PIDLIST_ABSOLUTE> absolute;
    std::vector<PCUITEMID_CHILD> children;
    HRESULT result = S_OK;
    for (const auto& file : files) {
        PIDLIST_ABSOLUTE item = nullptr;
        result = SHParseDisplayName(file.c_str(), nullptr, &item, 0, nullptr);
        if (FAILED(result)) break;
        absolute.push_back(item);
        children.push_back(ILFindLastID(item));
    }
    if (SUCCEEDED(result)) {
        result = SHCreateDataObject(parent, static_cast<UINT>(children.size()),
            children.data(), nullptr, IID_IDataObject, reinterpret_cast<void**>(object.Put()));
    }
    for (const auto item : absolute) CoTaskMemFree(item);
    CoTaskMemFree(parent);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SUCCEEDED(result) && object && SUCCEEDED(object->QueryGetData(&format));
}

const wchar_t kReadyScript[] = LR"JS(
(() => {
  let stage = "", overSent = false, expectedHit = null;
  const motionStages = ["blank","input","button","scroll","scrollbar"];
  const report = id => chrome.webview.postMessage({
    version:1, scope:"wallpaper", action:"like", id:"probe-" + id, value:true
  });
  const reportError = error => report("error-" + stage + "-" +
    String(error.message).replace(/[^A-Za-z0-9_.-]/g,"_").slice(0,60));
  const settle = () => new Promise(resolve =>
    requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve,120))));
  const click = selector => {
    const element = document.querySelector(selector);
    if (!element) throw new Error("missing " + selector);
    element.click();
  };
  document.addEventListener("dragover", event => {
    if (!overSent && event.isTrusted && Array.from(event.dataTransfer.types).includes("Files")) {
      overSent = true;
      report("over-" + stage);
    }
  }, true);
  document.addEventListener("drop", event => {
    if (event.isTrusted)
      report("drop-" + stage + "-" + event.dataTransfer.files.length);
  }, true);
  document.addEventListener("pointerdown", event => {
    if (!motionStages.includes(stage) || !event.isTrusted || event.button !== 0) return;
    if (!expectedHit || !(expectedHit === event.target || expectedHit.contains(event.target))) {
      reportError(new Error("pointerdown_wrong_target"));
      return;
    }
    report("pointer-" + stage);
  }, true);
  function reportTarget(x,y) {
    expectedHit = document.elementFromPoint(x,y);
    if (!expectedHit) throw new Error("missing_hit_target");
    report("hit-" + stage + "-" +
      (expectedHit.tagName + "_" + expectedHit.className).replace(/[^A-Za-z0-9_.]/g,"_").slice(0,50));
    report("target-" + stage + "-" + Math.round(x/innerWidth*10000) + "-" +
      Math.round(y/innerHeight*10000));
  }
  function motionTarget(next) {
    const grid = document.querySelector(".wallpaper-grid");
    if (!grid || grid.scrollHeight <= grid.clientHeight+80)
      throw new Error("fixture_must_really_scroll");
    if (next === "blank") {
      const x = innerWidth*.02, y = innerHeight*.5;
      const hit = document.elementFromPoint(x,y);
      if (!hit?.matches("#content,.ExperienceRoot,.MarketShell"))
        throw new Error("outer_blank_not_a_surface");
      for (let element=hit;element;element=element.parentElement)
        if (/(auto|scroll|overlay)/.test(getComputedStyle(element).overflowY))
          throw new Error("outer_blank_is_inside_scroller");
      reportTarget(x,y);
      return;
    }
    if (next === "input" || next === "button") {
      const element = document.querySelector(next === "input" ? ".SearchInput" :
        'button[data-view="wallpaperLocal"]');
      if (!element) throw new Error("missing_interactive_control");
      const rect = element.getBoundingClientRect();
      reportTarget(rect.left+rect.width*.5,rect.top+rect.height*.5);
      if (!element.contains(expectedHit)) throw new Error("control_is_occluded");
      return;
    }
    grid.scrollTop=0;
    const rect = grid.getBoundingClientRect();
    if (next === "scrollbar") {
      const width=grid.offsetWidth-grid.clientWidth;
      if (width<2) throw new Error("no_real_scrollbar_gutter");
      const thumb=Math.max(20,grid.clientHeight*grid.clientHeight/grid.scrollHeight);
      reportTarget(rect.right-width*.5,rect.top+Math.min(25,thumb*.5));
      if (expectedHit!==grid) throw new Error("scrollbar_not_hit");
      return;
    }
    // Pick an actual inter-card gap, not an interactive wallpaper card.
    for (let y=rect.top+24;y<rect.bottom-24;y+=24)
      for (let x=rect.left+16;x<rect.right-16;x+=8)
        if (document.elementFromPoint(x,y)===grid) {
          reportTarget(x,y);
          return;
        }
    throw new Error("no_visible_scrolling_blank");
  }
  async function navigate(next) {
    stage = next;
    overSent = false;
    if (next === "detail") {
      click('[data-view="wallpaperLocal"]');
      await settle();
      click('.wallpaper-card[data-wallpaper-id="fixture-first"]');
    } else {
      if (document.querySelector(".wallpaper-detail"))
        click('[data-action="back"]');
      click('[data-view="' + (next === "official" ? "wallpaperOfficial" : "wallpaperLocal") + '"]');
    }
    await settle();
    if (motionStages.includes(next)) {
      motionTarget(next);
      return;
    }
    if (next === "local") {
      chrome.webview.postMessage("v1|wallpaper|dropFiles");
      const fake = new File(["not an OS file"],"fake.png",{type:"image/png"});
      Object.defineProperty(fake,"path",{value:"C:\\spoofed-wallpaper.png"});
      try {
        chrome.webview.postMessageWithAdditionalObjects("v1|wallpaper|dropFiles",[fake]);
      } catch (error) {
        report("fake-local-" + String(error.name).replace(/[^A-Za-z0-9]/g,"").slice(0,20));
      }
      chrome.webview.postMessage({version:1,scope:"wallpaper",action:"dropFiles",
        paths:["C:\\spoofed-wallpaper.png"]});
      await new Promise(resolve => setTimeout(resolve,350));
      report("spoof-local-3");
    }
    const selector = next === "detail" ? ".wallpaper-detail" : ".WallpaperLibraryLayout";
    const surface = document.querySelector(selector);
    if (!surface) throw new Error("missing surface");
    const zone = !!surface.closest('[data-wallpaper-drop-zone="local"]');
    if (zone !== (next === "local" || next === "returned")) throw new Error("wrong zone");
    const bounds = surface.getBoundingClientRect();
    const x = Math.min(innerWidth-40, Math.max(40, bounds.left+bounds.width*.55));
    const y = Math.min(innerHeight-80, Math.max(80, bounds.top+bounds.height*.55));
    const hit = document.elementFromPoint(x,y);
    if (!hit || !surface.contains(hit)) throw new Error("occluded surface");
    reportTarget(x,y);
  }
  chrome.webview.addEventListener("message", event => {
    if (event.data?.type === "interaction-test")
      navigate(event.data.stage).catch(reportError);
    if (event.data?.type === "interaction-check")
      report("checked-" + stage + "-" +
        Math.round(document.querySelector(".wallpaper-grid")?.scrollTop || 0));
  });
  navigate("local").catch(reportError);
})();
)JS";

struct Probe {
    WebViewPageWindow window;
    fs::path root, profile;
    std::vector<fs::path> files;
    std::array<unsigned, kFileStages> callbacks{}, trustedOver{}, trustedDrop{};
    std::array<HRESULT, kFileStages> results{};
    std::array<DWORD, kFileStages> effects{};
    std::vector<HANDLE> browserProcesses;
    HWND observedWindow = nullptr;
    POINT originalPointer{}, target{};
    HWND originalForeground = nullptr;
    ULONGLONG started = GetTickCount64(), targetReady = 0, completedAt = 0;
    UINT_PTR timer = 0;
    unsigned ready = 0, stage = 0;
    bool failed = false, closing = false, busy = false, hasTarget = false, ranDrop = false;
    bool completed = false, profileOwned = false, rootOwned = false, spoofVerified = false;
    bool pointerSaved = false, motionStarted = false, motionJoined = false;
    bool motionCheckRequested = false, motionChecked = false, monitorMotion = false;
    bool nativeMoving = false, subclassInstalled = false;
    unsigned motionPassed = 0, pointerDown = 0, moveEntered = 0, moveExited = 0;
    int scrollPosition = 0;
    LONG maximumMotion = 0;
    RECT motionBefore{}, motionAfter{};
    std::thread motionThread;
    std::atomic<bool> motionDone{false}, motionCancel{false}, motionInputFailed{false};
    std::string failure;

    void StopMotion() {
        motionCancel = true;
        if (motionThread.joinable()) motionThread.join();
    }

    void Fail(const std::string& message) {
        if (!failed) {
            failure = message;
            std::cerr << "FAIL: " << message << '\n';
        }
        failed = true;
        StopMotion();
        closing = true;
        window.RequestClose();
    }

    bool Prepare() {
        const auto cwd = fs::current_path();
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) return false;
        executable.resize(length);
        profile = fs::path(executable).parent_path() / L"test-webview-profile" /
            std::to_wstring(GetCurrentProcessId());
        const auto relative = profile.lexically_relative(cwd);
        if (relative.empty() || relative.is_absolute() ||
            std::find(relative.begin(), relative.end(), fs::path(L"..")) != relative.end()) {
            Fail("run from the build directory: verification profile must stay inside cwd");
            return false;
        }
        if (fs::exists(profile)) {
            Fail("refusing to reuse or delete an existing verification profile");
            return false;
        }
        profileOwned = true;
        root = cwd / ("webview-wallpaper-interaction-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(started));
        if (!fs::create_directory(root)) return false;
        rootOwned = true;
        files = {root / L"first space.png", root / L"\u6d4b\u8bd5-second.png"};
        constexpr unsigned char png[] = {
            0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
            0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,0x00,0xb5,0x1c,0x0c,0x02,
            0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x64,0xf8,0x0f,0x00,0x01,0x05,
            0x01,0x01,0x27,0x18,0xe3,0x66,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
        };
        for (const auto& file : files) {
            std::ofstream stream(file, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(png), sizeof(png));
            if (!stream) return false;
        }
        originalForeground = GetForegroundWindow();
        pointerSaved = GetCursorPos(&originalPointer) != FALSE;
        if (!pointerSaved || (GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
            Fail("cannot isolate input while the pointer is unavailable or left button is held");
            return false;
        }
        return true;
    }

    void Snapshot(unsigned revision, bool scrolling = false) {
        const auto media = "https://lume-wallpaper.local/first%20space.png";
        Json state = {
            {"version",1},{"type","wallpaper-state"},{"revision",revision},{"status","idle"},
            {"error",""},{"loadStatus","fresh"},{"playerAvailable",false},
            {"catalogStatus","ready"},{"catalogError",""},{"displays",Json::array()},
            {"pauseFullscreen",false},{"likedIds",Json::array()},
            {"download",{{"id",""},{"status","idle"},{"received",0},{"total",0},{"error",""}}},
            {"catalog",Json::array({{{"id","fixture-official"},{"title","Official fixture"},
                {"kind","video"},{"thumbnail",media},{"url",media},{"width",1},{"height",1},
                {"bytes",68},{"libraryId",""}}})},
            {"library",Json::array({{{"id","fixture-first"},{"title","Local fixture"},{"kind","image"},
                {"thumbnail",media},{"url",media},{"bytes",68},{"missing",false},{"official",false}}})}
        };
        if (scrolling) for (int index=1;index<72;++index) {
            auto item=state["library"][0];
            item["id"]="fixture-scroll-"+std::to_string(index);
            item["title"]="Scrolling fixture "+std::to_string(index);
            state["library"].push_back(std::move(item));
        }
        window.Post(WideJson(state));
    }

    void OnWallpaper(const lume::ui::WallpaperPageCommand& command) {
        if (command.action != lume::ui::WallpaperAction::Like ||
            command.id.rfind("probe-", 0) != 0) return;
        std::cout << "DOM " << command.id << '\n';
        if (closing) {
            Fail("page callback arrived after close was requested");
            return;
        }
        std::string text = command.id.substr(6);
        std::replace(text.begin(), text.end(), '-', ' ');
        std::istringstream input(text);
        std::string kind, name;
        input >> kind >> name;
        if (stage >= kStages.size() || name != kStages[stage]) {
            Fail("unexpected page handshake stage: " + command.id);
            return;
        }
        if (kind == "error") Fail("ready script: " + command.id);
        else if (kind == "fake") {
            std::string error;
            input >> error;
            std::cout << "Fake File rejected by WebView2 API: " << error << '\n';
        } else if (kind == "spoof") {
            unsigned count = 0;
            input >> count;
            spoofVerified = count == 3 && callbacks[0] == 0 && !ranDrop;
            if (!spoofVerified) Fail("missing objects / fake File / string paths were not rejected");
            else std::cout << "PASS missing AdditionalObjects, fake File and string-only paths rejected\n";
        } else if (kind == "pointer") ++pointerDown;
        else if (kind == "checked") {
            input >> scrollPosition;
            motionChecked = static_cast<bool>(input);
        } else if (kind == "over" && stage < kFileStages) ++trustedOver[stage];
        else if (kind == "drop") {
            unsigned count = 0;
            input >> count;
            if (count != 2) Fail("trusted DOM drop did not contain both files");
            if (stage < kFileStages) ++trustedDrop[stage];
            else Fail("unexpected file drop during window movement checks");
        } else if (kind == "target") {
            int x = 0, y = 0;
            input >> x >> y;
            RECT client{};
            if (!input || x <= 0 || x >= 10000 || y <= 0 || y >= 10000 ||
                !GetClientRect(window.Handle(), &client)) {
                Fail("invalid DOM target geometry");
                return;
            }
            target = {MulDiv(x,client.right,10000), MulDiv(y,client.bottom,10000)};
            ClientToScreen(window.Handle(), &target);
            targetReady = GetTickCount64();
            hasTarget = true;
        }
    }

    void OnFiles(std::vector<std::wstring> paths) {
        if (closing || stage >= kFileStages) {
            Fail("file callback arrived after close");
            return;
        }
        ++callbacks[stage];
        if (!busy && !ranDrop) Fail("file callback arrived without any real OLE operation");
        if (stage != 0 && stage != 3) Fail("a non-local page accepted files");
        if (paths.size() != files.size()) Fail("native callback must contain exactly two paths");
        else for (std::size_t i = 0; i < files.size(); ++i) {
            if (paths[i] != files[i].wstring() || !fs::is_regular_file(files[i]))
                Fail("native file path/order differs from the actual OLE file");
        }
        std::cout << "NATIVE " << kStages[stage] << " paths=" << paths.size()
            << " exact=" << !failed << " (space and Unicode filenames)\n";
        Snapshot(stage + 1);
    }

    static LRESULT CALLBACK ObserveMotion(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
        UINT_PTR id, DWORD_PTR context) {
        auto& probe=*reinterpret_cast<Probe*>(context);
        if (message==WM_ENTERSIZEMOVE) {
            probe.nativeMoving=true;
            if (probe.monitorMotion) ++probe.moveEntered;
        } else if (message==WM_EXITSIZEMOVE) {
            probe.nativeMoving=false;
            if (probe.monitorMotion) ++probe.moveExited;
        }
        if (message==WM_NCDESTROY) RemoveWindowSubclass(hwnd,ObserveMotion,id);
        const auto result=DefSubclassProc(hwnd,message,wp,lp);
        if (message==WM_WINDOWPOSCHANGED && probe.monitorMotion) {
            RECT rect{};
            if (GetWindowRect(hwnd,&rect))
                probe.maximumMotion=std::max({probe.maximumMotion,
                    std::abs(rect.left-probe.motionBefore.left),std::abs(rect.top-probe.motionBefore.top)});
        }
        return result;
    }

    void StartMotion() {
        observedWindow=window.Handle();
        if (!subclassInstalled) {
            subclassInstalled=SetWindowSubclass(observedWindow,ObserveMotion,kMotionSubclass,
                reinterpret_cast<DWORD_PTR>(this))!=FALSE;
            if (!subclassInstalled) {Fail("cannot observe real HWND move messages");return;}
        }
        window.Present();
        const auto hit=WindowFromPoint(target);
        if (!IsWindowVisible(observedWindow) ||
            (hit!=observedWindow && !IsChild(observedWindow,hit)) ||
            !GetWindowRect(observedWindow,&motionBefore)) {
            Fail("real movement target is not visible or is occluded");
            return;
        }
        if (GetAsyncKeyState(VK_LBUTTON)&0x8000) {
            Fail("left button already held before test gesture");
            return;
        }
        pointerDown=moveEntered=moveExited=0;
        maximumMotion=0;
        motionCheckRequested=motionChecked=motionJoined=false;
        motionDone=motionCancel=motionInputFailed=false;
        monitorMotion=motionStarted=true;
        const bool vertical=std::string(kStages[stage])=="scrollbar";
        // Keep the UI thread pumping: production HTCAPTION enters the native
        // modal move loop, which must receive the delayed physical release.
        motionThread=std::thread([this,point=target,vertical] {
            if (!MovePointer(point)) motionInputFailed=true;
            Sleep(100);
            bool pressed=false;
            if (!motionCancel) {
                pressed=MouseButton(MOUSEEVENTF_LEFTDOWN);
                if (!pressed) motionInputFailed=true;
            }
            Sleep(300);
            for (int step=1;step<=5 && !motionCancel && pressed;++step) {
                POINT next{point.x+(vertical?0:step*10),point.y+(vertical?step*10:0)};
                if (!MovePointer(next)) motionInputFailed=true;
                Sleep(60);
            }
            if (pressed && !MouseButton(MOUSEEVENTF_LEFTUP)) motionInputFailed=true;
            motionDone=true;
        });
    }

    void NextStage() {
        monitorMotion=false;
        motionStarted=false;
        if (++stage==kStages.size()) {
            CaptureProcesses();
            completed=!failed;
            closing=true;
            window.RequestClose();
            return;
        }
        hasTarget=ranDrop=false;
        if (stage==kFileStages) Snapshot(20,true);
        window.Post(WideJson({{"type","interaction-test"},{"stage",kStages[stage]}}));
    }

    void TickMotion() {
        if (!motionStarted) {
            if (GetTickCount64()-targetReady>=200) StartMotion();
            return;
        }
        if (!motionDone || nativeMoving) return;
        if (!motionJoined) {
            if (motionThread.joinable()) motionThread.join();
            motionJoined=true;
            completedAt=GetTickCount64();
            return;
        }
        if (GetTickCount64()-completedAt<500) return;
        if (!motionCheckRequested) {
            motionCheckRequested=true;
            window.Post(WideJson({{"type","interaction-check"}}));
            return;
        }
        if (!motionChecked) return;
        monitorMotion=false;
        if (!GetWindowRect(observedWindow,&motionAfter)) {
            Fail("cannot measure the resulting HWND rectangle");
            return;
        }
        const LONG dx=motionAfter.left-motionBefore.left,dy=motionAfter.top-motionBefore.top;
        const bool blank=std::string(kStages[stage])=="blank";
        const bool scrollbar=std::string(kStages[stage])=="scrollbar";
        const bool sameSize=motionAfter.right-motionAfter.left==motionBefore.right-motionBefore.left &&
            motionAfter.bottom-motionAfter.top==motionBefore.bottom-motionBefore.top;
        const bool correct=blank ? std::abs(dx-50)<=2 && std::abs(dy)<=2 &&
            sameSize && moveEntered==1 && moveExited==1 :
            EqualRect(&motionBefore,&motionAfter) && maximumMotion==0 && moveEntered==0 && moveExited==0;
        const bool inputReleased=(GetAsyncKeyState(VK_LBUTTON)&0x8000)==0;
        std::cout << "MOVE " << kStages[stage] << " dx=" << dx << " dy=" << dy
            << " maxMotion=" << maximumMotion << " enter=" << moveEntered << " exit=" << moveExited
            << " trustedPointerDown=" << pointerDown << " scrollTop=" << scrollPosition
            << " buttonReleased=" << inputReleased << '\n';
        const bool restored=SetWindowPos(observedWindow,nullptr,motionBefore.left,motionBefore.top,
            motionBefore.right-motionBefore.left,motionBefore.bottom-motionBefore.top,
            SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
        RECT restoredRect{};
        const bool restoredExactly=restored && GetWindowRect(observedWindow,&restoredRect) &&
            EqualRect(&motionBefore,&restoredRect);
        if (!correct || motionInputFailed || !inputReleased || !restoredExactly ||
            (!scrollbar && pointerDown!=1) || (scrollbar && scrollPosition<=0)) {
            Fail("real held-button HWND movement/interaction mismatch in "+std::string(kStages[stage]));
            return;
        }
        ++motionPassed;
        std::cout << "PASS " << kStages[stage] << " HWND restored=1\n";
        NextStage();
    }

    void Drop() {
        if (!spoofVerified) {
            Fail("spoofed File refusal handshake was not observed");
            return;
        }
        observedWindow = window.Handle();
        window.Present();
        if (!IsWindowVisible(observedWindow)) {
            Fail("OS visibility/interactive-desktop restriction; real OLE drop NOT VERIFIED");
            return;
        }
        SetWindowPos(observedWindow,HWND_TOPMOST,0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        std::cout << "Target visible=1 foreground=" << (GetForegroundWindow() == observedWindow) << '\n';
        if (!MovePointer(target)) {
            Fail("SendInput pointer movement blocked; real OLE drop NOT VERIFIED");
            return;
        }
        const auto hit = WindowFromPoint(target);
        if (hit != observedWindow && !IsChild(observedWindow,hit)) {
            Fail("another window occludes the real WebView2 drop target");
            return;
        }
        ComPtr<IDataObject> data;
        if (!CreateFileDataObject(root, files, data)) {
            Fail("SHCreateDataObject did not expose real CF_HDROP files");
            return;
        }
        ComPtr<TimedDropSource> source(new TimedDropSource);
        std::atomic<bool> stop{false}, inputFailed{false};
        // The OLE loop needs real input to refresh its target and query the
        // source. Never synthesize DOM drop events or send WM_DROPFILES.
        std::thread pointer([&, point = target] {
            for (unsigned step = 0; step < 50 && !stop; ++step) {
                Sleep(40);
                POINT next{point.x + static_cast<LONG>(step % 2), point.y};
                if (!MovePointer(next)) inputFailed = true;
                if (step == 32 || step == 45) {
                    INPUT release{};
                    release.type = INPUT_MOUSE;
                    release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    if (SendInput(1,&release,sizeof(release)) != 1) inputFailed = true;
                }
            }
        });
        results[stage] = DoDragDrop(data.Get(), source.Get(), DROPEFFECT_COPY, &effects[stage]);
        stop = true;
        pointer.join();
        std::cout << "OLE " << kStages[stage] << " result=0x" << std::hex
            << static_cast<unsigned long>(results[stage]) << " effect=" << effects[stage]
            << std::dec << " queries=" << source->queries << " feedback=" << source->feedbacks
            << " copySeen=" << source->copyFeedback << '\n';
        if (inputFailed) Fail("OS blocked input during OLE operation");
        ranDrop = true;
        completedAt = GetTickCount64();
    }

    void CaptureProcesses() {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            Fail("cannot capture WebView2 process cleanup evidence");
            return;
        }
        std::vector<PROCESSENTRY32W> processes;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot,&entry)) do {
            processes.push_back(entry);
        } while (Process32NextW(snapshot,&entry));
        CloseHandle(snapshot);
        std::set<DWORD> children{GetCurrentProcessId()};
        bool added = true;
        while (added) {
            added = false;
            for (const auto& process : processes)
                if (children.count(process.th32ParentProcessID) &&
                    children.insert(process.th32ProcessID).second) added = true;
        }
        for (const auto& process : processes) {
            if (!children.count(process.th32ProcessID) ||
                _wcsicmp(process.szExeFile,L"msedgewebview2.exe") != 0) continue;
            const auto handle = OpenProcess(SYNCHRONIZE,FALSE,process.th32ProcessID);
            if (handle) browserProcesses.push_back(handle);
            else Fail("cannot observe a WebView2 child process");
        }
        std::cout << "WebView2 children observed=" << browserProcesses.size() << '\n';
        if (browserProcesses.empty()) Fail("no real WebView2 child process was observed");
    }

    void Tick() {
        if (GetTickCount64() - started > kDeadlineMs) {
            Fail("deadline exceeded; real OLE acceptance incomplete");
            return;
        }
        if (failed || busy || !hasTarget) return;
        if (stage >= kFileStages) {TickMotion();return;}
        if (!ranDrop) {
            if (GetTickCount64() - targetReady < 200) return;
            busy = true;
            Drop();
            busy = false;
            return;
        }
        if (GetTickCount64() - completedAt < 700) return;
        const bool accepted = stage == 0 || stage == 3;
        if (results[stage] != DRAGDROP_S_DROP || effects[stage] !=
            static_cast<DWORD>(accepted ? DROPEFFECT_COPY : DROPEFFECT_NONE) ||
            trustedOver[stage] != 1 || trustedDrop[stage] != (accepted ? 1u : 0u) ||
            callbacks[stage] != (accepted ? 1u : 0u)) {
            Fail("OLE effect / trusted DOM event / native callback mismatch in " +
                std::string(kStages[stage]));
            return;
        }
        std::cout << "PASS " << kStages[stage] << " trustedOver=" << trustedOver[stage]
            << " trustedDrop=" << trustedDrop[stage] << " nativeCallbacks=" << callbacks[stage] << '\n';
        NextStage();
    }

    bool Cleanup() {
        StopMotion();
        if (timer) KillTimer(nullptr,timer);
        if (window.Handle() || (observedWindow && IsWindow(observedWindow)))
            Fail("WebView2 host HWND survived Run");
        const auto deadline = GetTickCount64() + 10000;
        bool exited = browserProcesses.empty();
        do {
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message != WM_QUIT) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            exited = std::all_of(browserProcesses.begin(),browserProcesses.end(),
                [](HANDLE handle) { return WaitForSingleObject(handle,0) == WAIT_OBJECT_0; });
            if (exited) break;
            Sleep(25);
        } while (GetTickCount64() < deadline);
        for (const auto handle : browserProcesses) CloseHandle(handle);
        browserProcesses.clear();
        if (!exited) Fail("WebView2 children did not exit within cleanup deadline");
        bool removed = false;
        do {
            std::error_code error;
            if (rootOwned) fs::remove_all(root,error);
            if (profileOwned) fs::remove_all(profile,error);
            removed = (!rootOwned || !fs::exists(root)) && (!profileOwned || !fs::exists(profile));
            if (removed) break;
            Sleep(50);
        } while (GetTickCount64() < deadline);
        if (!removed) Fail("isolated fixture/profile cleanup failed");
        if (originalForeground && IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
        bool pointerRestored=false;
        if (pointerSaved) {
            if (!MovePointer(originalPointer)) Fail("cannot restore original mouse position");
            Sleep(30);
            POINT restored{};
            pointerRestored=GetCursorPos(&restored) && restored.x==originalPointer.x &&
                restored.y==originalPointer.y;
            if (!pointerRestored) Fail("original mouse position was not restored exactly");
        }
        std::cout << "CLEANUP hwndGone=" << (window.Handle() == nullptr)
            << " childrenExited=" << exited << " fixturesRemoved=" << removed
            << " pointerRestored=" << pointerRestored << '\n';
        return exited && removed;
    }
};

Probe* current = nullptr;
void CALLBACK TickProbe(HWND, UINT, UINT_PTR, DWORD) {
    if (!current) return;
    try { current->Tick(); }
    catch (const std::exception& error) { current->Fail(error.what()); }
}
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto ole = OleInitialize(nullptr);
    if (FAILED(ole)) {
        std::cerr << "OleInitialize failed; real OLE drop NOT VERIFIED\n";
        return 1;
    }
    Probe probe;
    current = &probe;
    int result = 1;
    try {
        if (probe.Prepare()) {
            probe.window.ConfigureWallpaper(
                [&](const lume::ui::WallpaperPageCommand& command) { probe.OnWallpaper(command); },
                probe.root.wstring(),
                [&](std::vector<std::wstring> paths) { probe.OnFiles(std::move(paths)); });
            probe.timer = SetTimer(nullptr,0,30,TickProbe);
            if (!probe.timer) probe.Fail("cannot install acceptance deadline timer");
            else result = probe.window.Run(GetModuleHandleW(nullptr), [&](lume::ui::PageCommand command) {
                if (command == lume::ui::PageCommand::Ready) {
                    ++probe.ready;
                    probe.Snapshot(0);
                }
                return std::wstring{};
            }, true, kReadyScript);
        } else probe.Fail("isolated fixture preparation failed");
    } catch (const std::exception& error) {
        probe.Fail(error.what());
    }
    const bool cleaned = probe.Cleanup();
    current = nullptr;
    OleUninitialize();
    if (result != 0 || probe.ready != 1 || !probe.completed || probe.failed || !cleaned ||
        probe.motionPassed!=kStages.size()-kFileStages) {
        std::cerr << "Real WebView2 OLE acceptance FAILED: result=" << result
            << " ready=" << probe.ready << " stages=" << probe.stage
            << " reason=" << probe.failure << '\n';
        return 1;
    }
    std::cout << "Real WebView2 OLE file and held-button HWND movement acceptance passed\n";
    return 0;
}
