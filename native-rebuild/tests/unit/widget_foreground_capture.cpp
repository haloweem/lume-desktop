// Development-only capture probe. It calls the production foreground painters
// with fixed inputs, without opening a desktop window or starting live services.
#include "platform/windows/almanac/almanac_view.h"
#include "platform/windows/performance/performance_view.h"
#include "platform/windows/organizer/organizer_view.h"
#include "platform/windows/organizer/organizer_text.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lume;
using namespace lume::platform::windows;
using Json=nlohmann::json;
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct ComScope {
    ComScope(){Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization failed");}
    ~ComScope(){CoUninitialize();}
};
Json Rect(WidgetRect r){return {{"x",r.x},{"y",r.y},{"width",r.width},{"height",r.height}};}
std::string Join(const std::vector<std::string_view>& values){
    std::string result;
    for(const auto value:values){if(!result.empty())result+=u8"、";result+=value;}
    return result;
}
void SavePixels(const std::filesystem::path& path,const WidgetBitmap& bitmap){
    Require(bitmap.pixels.size()==static_cast<std::size_t>(bitmap.width)*bitmap.height,"invalid native bitmap");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bitmap.pixels.data()),
              static_cast<std::streamsize>(bitmap.pixels.size()*sizeof(std::uint32_t)));
    Require(out.good(),"cannot save BGRA capture");
}
std::shared_ptr<const WidgetBitmap> FixtureIcon(){
    // The same immutable test pixels are passed to the native painter and the
    // original renderer. This does not test live Shell icon extraction.
    auto icon=std::make_shared<WidgetBitmap>();
    icon->width=48;icon->height=48;icon->pixels.resize(48*48);
    for(unsigned y=4;y<44;++y)for(unsigned x=8;x<40;++x){
        const bool line=(y>=16&&y<19)||(y>=24&&y<27)||(y>=32&&y<35);
        icon->pixels[y*48+x]=line&&x>=15&&x<(y>=32?27U:33U)?0xff8b9abaU:0xffe6edf9U;
    }
    return icon;
}
organizer::Collection Collection(){
    auto result=organizer::Defaults().collections.front();
    result.items={
        {"capture-1","C:\\LumeCapture\\Project",u8"项目资料",organizer::ItemKind::Folder},
        {"capture-2","C:\\LumeCapture\\Notes.txt",u8"会议记录",organizer::ItemKind::File},
        {"capture-3","C:\\LumeCapture\\Browser.lnk",u8"浏览器",organizer::ItemKind::Shortcut},
        {"capture-4","C:\\LumeCapture\\Plan.txt",u8"项目计划",organizer::ItemKind::File},
        {"capture-5","C:\\LumeCapture\\Pictures",u8"图片",organizer::ItemKind::Folder},
        {"capture-6","C:\\LumeCapture\\Guide.txt",u8"使用说明与快捷键",organizer::ItemKind::File},
    };
    return result;
}
Json CollectionData(const organizer::Collection& collection){
    Json items=Json::array();
    for(const auto& item:collection.items)
        items.push_back({{"id",item.id},{"path",item.path},{"name",item.name},
            {"kind",item.kind==organizer::ItemKind::Folder?"folder":
                    item.kind==organizer::ItemKind::Shortcut?"shortcut":"file"}});
    return {{"id",collection.id},{"title",collection.title},{"items",items},
        {"mode",collection.mode==organizer::Mode::Collapsed?"collapsed":"expanded"},
        {"layout",collection.layout==organizer::Layout::List?"list":"icons"}};
}
Json OrganizerGeometry(const OrganizerLayout& layout,const organizer::Collection& collection){
    Json cells=Json::array();
    for(const auto& cell:layout.cells)
        cells.push_back({{"id",collection.items.at(cell.index).id},{"index",cell.index},
            {"bounds",Rect(cell.bounds)}});
    return {{"title",Rect(layout.title)},{"search",Rect(layout.search)},
        {"searchInput",Rect(layout.searchInput)},{"viewport",Rect(layout.viewport)},
        {"maxScroll",layout.maxScroll},{"collapsed",layout.collapsed},{"cells",cells}};
}
class Capture final {
public:
    Capture(std::filesystem::path output,unsigned dpi):output_(std::move(output)),dpi_(dpi){
        std::filesystem::create_directories(output_);
    }
    void Add(Json scene,unsigned width,unsigned height,const std::function<void(WidgetCanvas&)>& draw){
        WidgetBitmap bitmap;
        Require(canvas_.Begin(static_cast<float>(width),static_cast<float>(height),dpi_),"canvas Begin failed");
        draw(canvas_);
        Require(canvas_.Finish(bitmap),"canvas Finish failed");
        Require(bitmap.width==width*dpi_/96&&bitmap.height==height*dpi_/96,"DPI output dimensions disagree");
        const auto name=scene.at("id").get<std::string>()+".bgra";
        SavePixels(output_/name,bitmap);
        scene["width"]=width;scene["height"]=height;scene["dpi"]=dpi_;
        scene["pixelWidth"]=bitmap.width;scene["pixelHeight"]=bitmap.height;scene["pixels"]=name;
        scenes_.push_back(std::move(scene));
    }
    void Finish(){
        wchar_t executable[32768]{};
        const auto length=GetModuleFileNameW(nullptr,executable,32768);
        Require(length>0&&length<32768,"cannot identify capture executable");
        Json manifest{
            {"version",1},{"dpi",dpi_},{"clock","2026-09-26T04:00:00Z"},{"timezone","Asia/Shanghai"},
            {"scope","Fixed data, production foreground painters, neutral background comparison. No native glass, desktop layering, live sampling, or Shell extraction acceptance."},
            {"nativeExecutable",std::filesystem::path(executable).u8string()},
            {"icon",{{"pixels","fixture-icon.bgra"},{"width",48},{"height",48}}},
            {"cases",scenes_}
        };
        std::ofstream out(output_/"foreground-manifest.json",std::ios::binary|std::ios::trunc);
        out<<manifest.dump(2);
        Require(out.good(),"cannot save capture manifest");
        std::cout<<"Captured "<<scenes_.size()<<" fixed foreground scenes at "<<dpi_<<" DPI\n";
    }
private:
    std::filesystem::path output_;
    unsigned dpi_;
    WidgetCanvas canvas_;
    Json scenes_=Json::array();
};
void Almanac(Capture& capture){
    constexpr calendar::Date date{2026,9,26};
    const auto day=almanac::Lookup(date);
    Require(day.has_value(),"fixed almanac date unavailable");
    AlmanacView view;view.SetDate(date);
    Json panels=Json::array();
    for(const auto& panel:view.Panels(320,152))
        panels.push_back({{"x",panel.x},{"y",panel.y},{"width",panel.width},{"height",panel.height}});
    capture.Add({{"id","almanac-default"},{"kind","almanac"},
        {"date",{{"year",date.year},{"month",date.month},{"day",date.day}}},
        {"expected",{{"#almanac-lunar",std::string(day->lunarDate)},{"#almanac-year",std::string(day->year)},
            {"#almanac-day-cycle",day->cycle},{"#almanac-yi",Join(day->yi)},{"#almanac-ji",Join(day->ji)}}},
        {"nativePanels",panels}},
        320,152,[&](WidgetCanvas& canvas){view.Draw(canvas,320,152);});
}
void Performance(Capture& capture){
    performance::Snapshot sample;
    sample.cpu=33.6;sample.gpu=18.2;sample.memory=62.5;
    sample.usedMemory=10ULL*1024*1024*1024;sample.totalMemory=16ULL*1024*1024*1024;
    sample.download=2.5*1024*1024;sample.upload=80.0*1024;
    Json values{{"cpu",*sample.cpu},{"gpu",*sample.gpu},{"memory",*sample.memory},
        {"usedMemory",sample.usedMemory},{"totalMemory",sample.totalMemory},
        {"download",*sample.download},{"upload",*sample.upload}};
    Json expected{
        {"#performance-cpu",OrganizerUtf8(PerformancePercent(sample.cpu))},
        {"#performance-gpu",OrganizerUtf8(PerformancePercent(sample.gpu))},
        {"#performance-memory",OrganizerUtf8(PerformancePercent(sample.memory))},
        {"#performance-memory-detail",OrganizerUtf8(PerformanceMemory(sample))},
        {"#performance-download",OrganizerUtf8(PerformanceSpeed(sample.download))},
        {"#performance-upload",OrganizerUtf8(PerformanceSpeed(sample.upload))},
        {"#performance-status",u8"实时"}
    };
    capture.Add({{"id","performance-default"},{"kind","performance"},{"status","ready"},
        {"snapshot",values},{"expected",expected}},
        340,216,[&](WidgetCanvas& canvas){DrawPerformance(canvas,340,216,sample,false);});
    for(auto it=expected.begin();it!=expected.end();++it)it.value()=it.key()=="#performance-status"?u8"暂不可用":"--";
    capture.Add({{"id","performance-unavailable"},{"kind","performance"},{"status","error"},
        {"snapshot",Json::object()},{"expected",expected}},
        340,216,[](WidgetCanvas& canvas){DrawPerformance(canvas,340,216,std::nullopt,true);});
}
void Organizer(Capture& capture,const std::shared_ptr<const WidgetBitmap>& icon){
    const auto original=Collection();
    const auto add=[&](const char* id,organizer::Collection collection,OrganizerViewState view,unsigned width,unsigned height){
        if(view.searchOpen){
            view.filtered=organizer::HasSearchTerms(view.query,OrganizerSearchKey);
            view.visible=organizer::Search(collection,view.query,OrganizerSearchKey);
        }
        Json visible=Json::array();
        const auto layout=LayoutOrganizer(static_cast<float>(width),static_cast<float>(height),collection,view);
        for(const auto& cell:layout.cells)visible.push_back(collection.items.at(cell.index).id);
        capture.Add({{"id",id},{"kind","organizer"},{"collection",CollectionData(collection)},
            {"search",{{"open",view.searchOpen},{"query",view.query},{"focused",false}}},
            {"scrollTop",view.scroll},
            {"expectedVisibleIds",visible},{"nativeLayout",OrganizerGeometry(layout,collection)}},
            width,height,[&](WidgetCanvas& canvas){
                DrawOrganizer(canvas,static_cast<float>(width),static_cast<float>(height),collection,std::nullopt,
                    [&](const organizer::Item&){return icon;},view);
            });
    };
    auto empty=original;empty.items.clear();
    add("organizer-empty",empty,{},420,392);
    add("organizer-icons",original,{},420,392);
    auto collapsed=original;collapsed.mode=organizer::Mode::Collapsed;
    add("organizer-collapsed",collapsed,{},180,216);
    OrganizerViewState search;search.searchOpen=true;search.query=u8"项目";
    add("organizer-search",original,search,420,392);
    search.query=u8"不存在";
    add("organizer-search-empty",original,search,420,392);
    auto list=original;list.layout=organizer::Layout::List;
    add("organizer-list",list,{},420,392);
    auto scrolling=original;
    for(unsigned i=6;i<20;++i)
        scrolling.items.push_back({"capture-"+std::to_string(i+1),"C:\\LumeCapture\\Scroll-"+std::to_string(i+1),
            u8"滚动项目 "+std::to_string(i+1),organizer::ItemKind::File});
    OrganizerViewState middle;middle.scroll=47;
    add("organizer-icons-scrolled",scrolling,middle,420,392);
    scrolling.layout=organizer::Layout::List;middle.scroll=17;
    add("organizer-list-scrolled",scrolling,middle,420,392);
}
}
int main(int argc,char** argv){
    try{
        Require((argc==3||argc==5)&&std::string(argv[1])=="--capture",
                "Usage: lume-widget-foreground-capture --capture <directory> [--dpi 96|120|144|192]");
        unsigned dpi=96;
        if(argc==5){
            Require(std::string(argv[3])=="--dpi","expected --dpi");
            const std::string value=argv[4];
            Require(value=="96"||value=="120"||value=="144"||value=="192","supported DPI: 96, 120, 144, 192");
            dpi=static_cast<unsigned>(std::stoul(value));
        }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ComScope com;
        const auto output=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        Capture capture(output,dpi);
        const auto icon=FixtureIcon();SavePixels(output/"fixture-icon.bgra",*icon);
        Almanac(capture);Performance(capture);Organizer(capture,icon);capture.Finish();
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Foreground capture failed: "<<error.what()<<'\n';return 1;
    }
}
