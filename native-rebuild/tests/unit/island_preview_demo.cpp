#include "platform/windows/dynamic_island/island_preview.h"
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <string>
using namespace lume::platform::windows;
int wmain(int argc,wchar_t** argv) {
    if(argc!=9)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 3;
    int result=0;
    {
        const auto shape=[](const wchar_t* text){return std::wstring(text)==L"standard"?IslandShape::Standard:IslandShape::Capsule;};
        const auto size=[](const wchar_t* text){return std::wstring(text)==L"small"?IslandSize::Small:std::wstring(text)==L"large"?IslandSize::Large:IslandSize::Standard;};
        const auto expanded=[](const wchar_t* text){return std::wstring(text)==L"expanded";};
        IslandDisplay from;from.shape=shape(argv[2]);from.size=size(argv[3]);
        from.material=std::wstring(argv[8])==L"liquid-glass"?IslandMaterial::Liquid:std::wstring(argv[8])==L"frosted-glass"?IslandMaterial::Frosted:IslandMaterial::None;
        auto to=from;to.shape=shape(argv[5]);to.size=size(argv[6]);
        const auto output=std::filesystem::path(argv[1]);std::filesystem::create_directories(output);
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        const auto wallpaper=std::filesystem::path(exe).parent_path()/L"ui/assets/island-preview/forest.jpg";
        IslandWindow window(GetModuleHandleW(nullptr),{});IslandPreviewRenderer renderer;
        auto frame=window.Preview(from,expanded(argv[4]));
        unsigned count=0;
        do {
            const auto image=renderer.Render(frame,to,wallpaper.wstring());
            if(!IslandPreviewRenderer::WritePng(image,(output/(std::to_wstring(count)+L".png")).wstring())){result=4;break;}
            ++count;
            if(count>1&&!frame.animating)break;
            frame=window.Preview(to,expanded(argv[7]),1.f/30);
        }while(count<90);
        if(!result){std::ofstream manifest(output/L"count.txt");manifest<<count;}
    }
    CoUninitialize();return result;
}
