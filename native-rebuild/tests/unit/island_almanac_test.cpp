#include "apps/shell/dynamic_island_session.h"
#include "platform/windows/dynamic_island/island_almanac_card.h"
#include "platform/windows/dynamic_island/island_preview.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
using namespace lume;
using namespace lume::platform::windows;
namespace {
void Check(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
struct Files final:settings::IFileStore {
    std::map<std::string,std::string> values;bool fail=false;
    bool Read(const std::string& key,std::string& out)const override{const auto i=values.find(key);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& key)const override{return values.count(key)!=0;}
    bool Write(const std::string& key,const std::string& value)override{if(fail)return false;values[key]=value;return true;}
    bool Rename(const std::string& a,const std::string& b)override{if(fail||!Exists(a))return false;values[b]=values[a];values.erase(a);return true;}
    bool Remove(const std::string& key)override{values.erase(key);return true;}
};
void Save(const WidgetBitmap& frame,const std::filesystem::path& file){
    Check(frame.width&&frame.height&&frame.pixels.size()==static_cast<std::size_t>(frame.width)*frame.height,"complete native frame");
    Gdiplus::Bitmap image(frame.width,frame.height,static_cast<INT>(frame.width*4),PixelFormat32bppPARGB,
                         reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(image.Save(file.c_str(),&png)==Gdiplus::Ok,"write native PNG");
}
}
int main(int argc,char** argv){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder");
    {
        const auto day=ReadIslandAlmanac({2026,10,16});
        Check(day.available&&day.lunar==L"九月初七"&&day.year==L"丙午年","reference date uses actual offline lunar data");
        Check(day.yi==std::array<std::wstring,4>{L"祭祀",L"开光",L"出行",L"解除"},"original ordered activities");
        Check(day.directions==std::array<std::wstring,4>{L"正南",L"东南",L"东北",L"东南"},"original reference day directions");
        Check(ReadIslandAlmanac({2023,3,22}).lunar==L"闰二月初一","leap lunar month survives presentation");
        Check(!ReadIslandAlmanac({2101,1,1}).available&&!ReadIslandAlmanac({2026,2,29}).available,"unsupported dates do not invent almanac facts");
        using ui::PageCommand;
        Check(ui::ParsePageCommand(L"v1|dynamicIsland|calendarStyle|default")==PageCommand::IslandCalendarDefault&&
              ui::ParsePageCommand(L"v1|dynamicIsland|calendarStyle|chinese-almanac")==PageCommand::IslandCalendarChineseAlmanac,"canonical calendar style commands");
        for(const auto* invalid:{L"",L"Chinese-Almanac",L"unknown",L"chinese-almanac|extra",L"default "})
            Check(ui::ParsePageCommand(std::wstring(L"v1|dynamicIsland|calendarStyle|")+invalid)==PageCommand::Invalid,"strict calendar style values");
        Files files;shell::DynamicIslandSessionOptions options;options.preferencesPath="almanac.json";
        const unsigned layout=dynamic_island::EncodeExpandedLayout({dynamic_island::ExpandedWidget::Clock,dynamic_island::ExpandedWidget::CalendarWeather});
        const auto legacy=nlohmann::json{{"version",1},{"expandedLayout",layout},{"clockStyle","pixel"},{"shape","standard"},{"size","small"},{"autoHide",false}};
        files.values["almanac-display.json"]=legacy.dump();
        {
            shell::DynamicIslandSession session(GetModuleHandleW(nullptr),options,&files);
            Check(session.Restore()&&nlohmann::json::parse(session.State())["calendarStyle"]=="default"&&files.values["almanac-display.json"]==legacy.dump(),"legacy preferences retain default without rewriting");
            files.fail=true;
            const auto rejected=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandCalendarChineseAlmanac));
            Check(!rejected["saved"]&&rejected["calendarStyle"]=="default"&&files.values["almanac-display.json"]==legacy.dump(),"failed save rolls back calendar style");
            files.fail=false;
            const auto saved=nlohmann::json::parse(*session.HandlePage(PageCommand::IslandCalendarChineseAlmanac));
            Check(saved["saved"]&&saved["calendarStyle"]=="chinese-almanac"&&saved["clockStyle"]=="pixel"&&saved["expandedLayout"]==layout,"calendar style saves independently of clock and widget ordering");
            session.Shutdown();
        }
        {
            shell::DynamicIslandSession restored(GetModuleHandleW(nullptr),options,&files);
            Check(restored.Restore()&&nlohmann::json::parse(restored.State())["calendarStyle"]=="chinese-almanac","calendar style survives session restart");
            restored.HandlePage(PageCommand::IslandResetAppearance);
            Check(nlohmann::json::parse(restored.State())["calendarStyle"]=="chinese-almanac","appearance reset retains the chosen calendar style");
            restored.Shutdown();
        }
        std::filesystem::path output;
        if(argc==2){output=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(output);}
        WidgetCanvas canvas;
        for(bool english:{false,true}){
            Check(canvas.Begin(IslandAlmanacWidth,IslandAlmanacHeight,192),"paper card canvas");
            DrawIslandAlmanacCard(canvas,{0,0,IslandAlmanacWidth,IslandAlmanacHeight},day,L"06:24",L"18:01",english,1);
            WidgetBitmap frame;Check(canvas.Finish(frame),"native paper card render");
            Check((frame.pixels.front()>>24)==0&&(frame.pixels[(frame.height/2)*frame.width+frame.width/2]>>24)>240,
                  "rounded transparent corners and opaque paper body");
            if(!output.empty())Save(frame,output/(english?L"style-calendar-chinese-almanac-en.png":L"style-calendar-chinese-almanac.png"));
        }
        IslandWindow window(GetModuleHandleW(nullptr),{});IslandDisplay display;
        display.expandedWidgets={dynamic_island::ExpandedWidget::CalendarWeather};
        const auto original=window.Preview(display,true);
        display.calendarStyle=IslandCalendarStyle::ChineseAlmanac;
        const auto almanac=window.Preview(display,true);
        Check(almanac.width&&almanac.height&&almanac.pixels.pixels!=original.pixels.pixels&&!window.Handle(),"real island renderer selects the new card without creating a window");
        for(const auto size:{IslandSize::Small,IslandSize::Standard,IslandSize::Large}){
            display.size=size;const auto frame=window.Preview(display,true);
            Check(frame.pixels.pixels.size()==static_cast<std::size_t>(frame.width)*frame.height&&frame.height>frame.width*.8f,"all island sizes retain the complete calendar card");
        }
        display.size=IslandSize::Standard;
        const auto single=window.Preview(display,true);
        display.expandedWidgets={dynamic_island::ExpandedWidget::Clock,dynamic_island::ExpandedWidget::CalendarWeather,dynamic_island::ExpandedWidget::Music};
        const auto combined=window.Preview(display,true);
        Check(combined.width>single.width&&combined.height==single.height&&!window.Handle(),"mixed widget row centers shorter modules beside the complete almanac");
        if(!output.empty()){
            Save(single.pixels,output/L"island-almanac-single.png");
            Save(combined.pixels,output/L"island-almanac-combined.png");
            IslandPreviewRenderer renderer;
            const auto scene=renderer.Render(combined,display,{});
            Check(scene.width==960&&scene.height==300,"production island preview scene fits standard-size mixed row");
            Save(scene,output/L"island-almanac-scene.png");
        }
        window.ReleasePreview();
    }
    Gdiplus::GdiplusShutdown(token);CoUninitialize();
    std::cout<<"PASS Chinese almanac data, protocol, persistence, rollback and native island rendering\n";
}
