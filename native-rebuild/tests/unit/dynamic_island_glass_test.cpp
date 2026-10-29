#include "platform/windows/dynamic_island/island_window.h"
#include <objbase.h>
#include <cstdlib>
#include <iostream>

// The island's liquid and frosted glass must actually appear: a visible
// LumeGlass surface directly below the island, covering the capsule.
using namespace lume;
using platform::windows::IslandDisplay;
using platform::windows::IslandMaterial;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
void Pump(unsigned ms){const auto end=GetTickCount64()+ms;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);}
struct Surface{HWND owner;HWND found;};
HWND GlassOf(HWND owner){
    Surface s{owner,nullptr};
    EnumWindows([](HWND w,LPARAM l)->BOOL{auto& s=*reinterpret_cast<Surface*>(l);
        if(GetPropW(w,L"LumeGlassSurfaceOwner")==reinterpret_cast<HANDLE>(s.owner)){s.found=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));
    return s.found;
}
bool GlassShown(HWND island){
    const HWND glass=GlassOf(island);
    return glass&&IsWindowVisible(glass)&&IsWindowVisible(island);
}
void Material(IslandMaterial material,bool topmost,const char* reason){
    platform::windows::IslandWindow window(GetModuleHandleW(nullptr),{});
    IslandDisplay display;display.material=material;display.topmost=topmost;
    widget::Preferences prefs;prefs.enabled=true;
    Check(window.Start(prefs,display),"island starts");
    bool shown=false;
    for(int i=0;i<200&&!shown;++i){Pump(25);shown=GlassShown(window.Handle());}
    if(!shown){
        const HWND glass=GlassOf(window.Handle());
        std::cerr<<reason<<": island visible="<<IsWindowVisible(window.Handle())<<" shown="<<window.Shown()
                 <<" glass="<<glass<<" glassVisible="<<(glass&&IsWindowVisible(glass))<<'\n';
    }
    Check(shown,reason);
    // Optional visual check: hold the settled glass on screen for a screenshot.
    if(const char* hold=std::getenv("LUME_ISLAND_GLASS_HOLD_MS"))Pump(static_cast<unsigned>(std::atoi(hold)));
    // The capsule changes size when expanded; the glass must stay.
    window.Expand(true);Pump(900);
    Check(GlassShown(window.Handle()),"glass stays when expanded");
    window.Stop();
}
// Choosing a glass in settings or the menu while the island is running.
void Switch(IslandMaterial material,const char* reason){
    platform::windows::IslandWindow window(GetModuleHandleW(nullptr),{});
    IslandDisplay display;
    widget::Preferences prefs;prefs.enabled=true;
    Check(window.Start(prefs,display),"island starts black");
    Pump(1500);
    Check(window.Shown()&&!GlassOf(window.Handle()),"black island has no glass");
    display.material=material;
    Check(window.SetDisplay(display),"material change accepted");
    bool shown=false;
    for(int i=0;i<200&&!shown;++i){Pump(25);shown=GlassShown(window.Handle());}
    if(!shown){
        const HWND glass=GlassOf(window.Handle());
        std::cerr<<reason<<": glass="<<glass<<" glassVisible="<<(glass&&IsWindowVisible(glass))<<'\n';
    }
    Check(shown,reason);
    display.material=IslandMaterial::None;
    Check(window.SetDisplay(display),"back to black");
    Pump(300);
    const HWND glass=GlassOf(window.Handle());
    Check(!glass||!IsWindowVisible(glass),"black removes the glass");
    window.Stop();
}
}
int main(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    Material(IslandMaterial::Liquid,true,"liquid glass above windows");
    Material(IslandMaterial::Frosted,true,"frosted glass above windows");
    Material(IslandMaterial::Liquid,false,"liquid glass in the desktop layer");
    Switch(IslandMaterial::Liquid,"switching a running island to liquid glass");
    Switch(IslandMaterial::Frosted,"switching a running island to frosted glass");
    CoUninitialize();
    std::cout<<"dynamic island glass test passed\n";
    return 0;
}
