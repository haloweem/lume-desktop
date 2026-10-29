// 独立验证：C++17，包含目录 native-rebuild，定义 UNICODE/_UNICODE/
// WIN32_LEAN_AND_MEAN/NOMINMAX。编译本文件，以及 organizer_motion.cpp、
// organizer_view.cpp、organizer_text.cpp、widget_canvas.cpp、organizer_model.cpp。
// Windows 链接库：d2d1、dwrite、windowscodecs、ole32、normaliz、uuid。
// 不传参数只验证行为与真实 Canvas 像素，不写文件；--capture <directory>
// 在验证通过后导出 72 张 BMP，覆盖 grid/list、96/144/192 DPI 与各运动阶段。
// BMP 使用底行在前的 32 位 BGRA，将预乘透明像素正确合成到固定底色；
// 图标是缓存的确定性文件夹/文档/应用测试素材，生产绘制仍复用现有图标回调。
#include "platform/windows/organizer/organizer_motion.h"
#include "organizer_viewport_test.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

using namespace lume::platform::windows;
namespace {
void Check(bool value,const char* message){
    if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
bool Near(float a,float b,float epsilon=.001f){return std::abs(a-b)<=epsilon;}
bool Same(WidgetRect a,WidgetRect b){
    return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.width,b.width)&&Near(a.height,b.height);
}
lume::organizer::Collection Collection(){
    lume::organizer::Collection result;result.id="motion";result.title="Organizer";result.iconScale=100;
    for(unsigned i=0;i<20;++i)result.items.push_back({"item-"+std::to_string(i),"C:\\test", "Name "+std::to_string(i)});
    return result;
}
const OrganizerMotionCell& Cell(const OrganizerMotionFrame& frame,std::size_t index){
    const auto found=std::find_if(frame.cells.begin(),frame.cells.end(),[&](const auto& cell){return cell.index==index;});
    Check(found!=frame.cells.end(),"motion frame contains requested item");return *found;
}
WidgetBitmap Paint(const lume::organizer::Collection& collection,const OrganizerViewState& view,
    const OrganizerMotionFrame* frame,unsigned dpi=96,const OrganizerImage& image={}){
    WidgetCanvas canvas;WidgetBitmap result;
    Check(canvas.Begin(420,392,dpi),"real canvas begins");
    DrawOrganizer(canvas,420,392,collection,{},image,view,frame);
    Check(canvas.Finish(result),"real canvas finishes");return result;
}
std::size_t Difference(const WidgetBitmap& a,const WidgetBitmap& b,WidgetRect region,unsigned dpi){
    std::size_t changed=0;
    for(unsigned y=0;y<a.height;++y)for(unsigned x=0;x<a.width;++x){
        const float dx=(x+.5f)*96/dpi,dy=(y+.5f)*96/dpi;
        if(dx>=region.x&&dx<region.x+region.width&&dy>=region.y&&dy<region.y+region.height){
            const auto i=static_cast<std::size_t>(y)*a.width+x;changed+=a.pixels[i]!=b.pixels[i];
        }
    }
    return changed;
}
void CheckClip(const WidgetBitmap& actual,const WidgetBitmap& base,WidgetRect clip,unsigned dpi){
    for(unsigned y=0;y<actual.height;++y)for(unsigned x=0;x<actual.width;++x){
        const float dx=(x+.5f)*96/dpi,dy=(y+.5f)*96/dpi;
        if(dx>=clip.x&&dx<clip.x+clip.width&&dy>=clip.y&&dy<clip.y+clip.height)continue;
        const auto i=static_cast<std::size_t>(y)*actual.width+x;
        Check(actual.pixels[i]==base.pixels[i],"motion, labels, feedback and badges remain clipped to viewport");
    }
}
std::uint32_t CompositeOver(std::uint32_t source,std::uint32_t background=0xff182235u){
    const unsigned alpha=source>>24;
    if(alpha==255)return source;
    const unsigned inverse=255-alpha;
    const auto blend=[&](unsigned sourceChannel,unsigned backgroundChannel){
        return std::min(255u,sourceChannel+(backgroundChannel*inverse+127u)/255u);
    };
    return 0xff000000u|(blend((source>>16)&255,(background>>16)&255)<<16)|
        (blend((source>>8)&255,(background>>8)&255)<<8)|blend(source&255,background&255);
}
bool SaveBmp(const std::filesystem::path& path,const WidgetBitmap& source){
    if(!source.width||!source.height||source.pixels.size()!=static_cast<std::size_t>(source.width)*source.height)return false;
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);
    file.bfSize=file.bfOffBits+source.width*source.height*4;
    info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(source.width);
    info.biHeight=static_cast<LONG>(source.height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    info.biSizeImage=source.width*source.height*4;
    std::vector<std::uint32_t> pixels(source.pixels.size());
    for(std::size_t i=0;i<pixels.size();++i)pixels[i]=CompositeOver(source.pixels[i]);
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if(!output)return false;
    output.write(reinterpret_cast<const char*>(&file),sizeof(file));
    output.write(reinterpret_cast<const char*>(&info),sizeof(info));
    for(unsigned row=source.height;row>0;--row)
        output.write(reinterpret_cast<const char*>(pixels.data()+static_cast<std::size_t>(row-1)*source.width),source.width*4);
    return static_cast<bool>(output);
}
std::shared_ptr<const WidgetBitmap> CaptureIcon(const lume::organizer::Item& item){
    static std::unordered_map<std::string,std::shared_ptr<const WidgetBitmap>> cache;
    const auto found=cache.find(item.id);if(found!=cache.end())return found->second;
    unsigned hash=0;for(const auto c:item.id)hash=hash*31+static_cast<unsigned char>(c);
    constexpr std::uint32_t palette[]={0x4c95e8,0x3ca77e,0xde6585,0x976ce2,0xe89c47};
    const auto accent=palette[hash%5];
    WidgetCanvas canvas;auto bitmap=std::make_shared<WidgetBitmap>();
    Check(canvas.Begin(48,48,144),"capture icon fixture begins");
    if(hash%3==0){
        canvas.Rounded({3,8,19,10},3,0xe3ae37);canvas.Rounded({3,14,42,29},4,0xf4c35c);
        canvas.Line(7,19,41,19,0xffe4a1,.9f,2);
    }else if(hash%3==1){
        canvas.Rounded({7,3,34,43},3,0xe9eff8);canvas.Rounded({12,9,24,9},2,accent);
        for(unsigned i=0;i<3;++i)canvas.Line(13,25+i*6.f,34-i*3.f,25+i*6.f,0x8e9bab,1,2);
    }else{
        canvas.Rounded({3,3,42,42},8,accent);
        canvas.Text(std::wstring(1,static_cast<wchar_t>(L'A'+hash%26)),{3,2,42,42},26,
            0xffffff,1,WidgetTextWeight::Semibold,WidgetTextAlign::Center);
    }
    Check(canvas.Finish(*bitmap),"capture icon fixture finishes");cache.emplace(item.id,bitmap);return bitmap;
}
void CaptureOne(const std::filesystem::path& directory,const std::string& name,const WidgetBitmap& bitmap){
    const auto path=directory/(name+".bmp");
    Check(SaveBmp(path,bitmap),"capture BMP writes as 32-bit BGRA");
    std::ifstream input(path,std::ios::binary);BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    input.read(reinterpret_cast<char*>(&file),sizeof(file));input.read(reinterpret_cast<char*>(&info),sizeof(info));
    Check(input&&file.bfType==0x4d42&&info.biWidth==static_cast<LONG>(bitmap.width)&&
        info.biHeight==static_cast<LONG>(bitmap.height)&&info.biBitCount==32&&info.biCompression==BI_RGB&&
        file.bfSize==std::filesystem::file_size(path),"BMP header, row order and length are valid");
    std::uint32_t first=0;input.read(reinterpret_cast<char*>(&first),sizeof(first));
    Check(first==CompositeOver(bitmap.pixels[static_cast<std::size_t>(bitmap.height-1)*bitmap.width]),
        "BMP begins with bottom row, with premultiplied BGRA composited onto opaque background");
}
OrganizerViewState TabView(std::vector<std::wstring> names,std::size_t active){
    OrganizerViewState view;view.tabs=std::move(names);view.activeTab=active;return view;
}
void TabStrip(){
    // A tab group replaces the title with clickable tabs between the grid and
    // search glyphs; a single collection keeps the original header exactly.
    const auto collection=Collection();
    const auto single=LayoutOrganizer(420,392,collection,{});
    Check(single.tabs.empty()&&single.title.width>0,"single collection keeps the title header");
    const auto view=TabView({L"桌面收纳",L"游戏",L"工作资料"},1);
    const auto layout=LayoutOrganizer(420,392,collection,view);
    Check(layout.tabs.size()==3&&layout.tabStrip.x>=44&&layout.tabStrip.x+layout.tabStrip.width<=layout.search.x,
        "tab strip sits between the grid glyph and search");
    Check(Same(layout.title,layout.tabs[1]),"rename editor targets the active tab");
    Check(Same(layout.viewport,single.viewport),"tabs do not move the icon grid");
    const auto hit=HitOrganizer(420,392,collection,layout.tabs[2].x+5,layout.tabs[2].y+15,view);
    Check(hit.kind==OrganizerHit::Kind::Tab&&hit.index==2,"tab hit reports its index");
    std::vector<std::wstring> many;for(unsigned i=0;i<8;++i)many.push_back(L"收纳 "+std::to_wstring(i+1));
    auto wide=TabView(many,7);const auto overflow=LayoutOrganizer(420,392,collection,wide);
    Check(overflow.maxTabScroll>0,"many tabs overflow and scroll");
    wide.tabScroll=overflow.maxTabScroll;const auto scrolled=LayoutOrganizer(420,392,collection,wide);
    Check(scrolled.tabs.back().x+scrolled.tabs.back().width<=scrolled.tabStrip.x+scrolled.tabStrip.width+.01f,
        "scrolling reveals the last tab inside the strip");
    const auto plain=Paint(collection,{},nullptr);const auto tabs=Paint(collection,view,nullptr);
    Check(Difference(plain,tabs,{44,18,320,36},96)>200,"tab strip pixels differ from the single title");
    Check(Difference(plain,tabs,single.viewport,96)==0,"tabs leave the icon grid pixels unchanged");
}
void CaptureVisuals(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);
    auto collection=Collection();
    const OrganizerImage icon=[](const auto& item){return CaptureIcon(item);};
    {
        auto grouped=collection;grouped.title=u8"桌面";grouped.iconScale=70;
        OrganizerViewState view;view.tabs={L"桌面",L"文件"};
        for(std::size_t tab=0;tab<2;++tab){
            view.activeTab=tab;WidgetCanvas canvas;WidgetBitmap bitmap;
            Check(canvas.Begin(260,680,120),"default tabbed organizer canvas begins");
            DrawOrganizer(canvas,260,680,grouped,{},icon,view);Check(canvas.Finish(bitmap),"default tabbed organizer canvas finishes");
            CaptureOne(directory,tab?"default-files-70":"default-desktop-70",bitmap);
        }
    }
    for(const int width:{120,180,420})for(const int scale:{50,100,120}){
        auto scaled=collection;scaled.iconScale=scale;
        WidgetCanvas canvas;WidgetBitmap bitmap;
        Check(canvas.Begin(static_cast<float>(width),392,96),"scaled organizer canvas begins");
        DrawOrganizer(canvas,static_cast<float>(width),392,scaled,{},icon);
        Check(canvas.Finish(bitmap),"scaled organizer canvas finishes");
        CaptureOne(directory,"organizer_width_"+std::to_string(width)+"_scale_"+std::to_string(scale),bitmap);
    }
    for(const unsigned dpi:{96u,144u}){
        const auto suffix=std::to_string(dpi)+"dpi";
        CaptureOne(directory,"tabs_single_"+suffix,Paint(collection,{},nullptr,dpi,icon));
        CaptureOne(directory,"tabs_two_"+suffix,Paint(collection,TabView({L"桌面收纳",L"游戏"},1),nullptr,dpi,icon));
        auto hover=TabView({L"桌面收纳",L"游戏",L"工作资料"},0);hover.hoverTab=2;
        CaptureOne(directory,"tabs_three_hover_"+suffix,Paint(collection,hover,nullptr,dpi,icon));
        std::vector<std::wstring> many;for(unsigned i=0;i<8;++i)many.push_back(L"收纳 "+std::to_wstring(i+1));
        auto scrolled=TabView(many,5);scrolled.tabScroll=200;
        CaptureOne(directory,"tabs_many_scrolled_"+suffix,Paint(collection,scrolled,nullptr,dpi,icon));
        auto lifted=TabView({L"桌面收纳",L"游戏",L"工作资料"},0);lifted.liftedTab=1;
        CaptureOne(directory,"tabs_lifted_"+suffix,Paint(collection,lifted,nullptr,dpi,icon));
        auto merge=OrganizerViewState{};merge.mergeTarget=true;
        CaptureOne(directory,"tabs_merge_target_"+suffix,Paint(collection,merge,nullptr,dpi,icon));
    }
    for(const bool list:{false,true})for(const unsigned dpi:{96u,144u,192u}){
        collection.layout=list?lume::organizer::Layout::List:lume::organizer::Layout::Icons;
        const auto mode=list?"list":"grid";
        OrganizerViewState view;view.scroll=list?13.f:37.f;
        const auto layout=LayoutOrganizer(420,392,collection,view);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_baseline",Paint(collection,view,nullptr,dpi,icon));
        auto selectedView=view;selectedView.selection={1,3,5};
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_selection",Paint(collection,selectedView,nullptr,dpi,icon));
        OrganizerMotion hoverMotion;
        const auto hovered=layout.cells[list?2:5].bounds;
        const OrganizerPoint hover{hovered.x+(list?21.f:41.f),hovered.y+(list?21.f:30.5f)};
        hoverMotion.Update(collection,layout,hover,{},"",{},.016f,false);
        for(unsigned i=0;i<180&&hoverMotion.Active();++i)hoverMotion.Update(collection,layout,hover,{},"",{},1.f/120,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_hover",Paint(collection,view,
            &hoverMotion.Update(collection,layout,hover,{},"",{},0,false),dpi,icon));
        for(const int scale:{70,100}){
            // The highlight tile with the magnified hovered icon, as the session draws it.
            if(list)break;
            auto sized=collection;sized.iconScale=scale;
            const auto sizedLayout=LayoutOrganizer(420,392,sized,view);
            const auto cell=sizedLayout.cells[5].bounds;
            const OrganizerPoint at{cell.x+cell.width/2,cell.y+30.5f*scale/100};
            OrganizerMotion motion;motion.Update(sized,sizedLayout,at,{},"",{},.016f,false);
            for(unsigned i=0;i<180&&motion.Active();++i)motion.Update(sized,sizedLayout,at,{},"",{},1.f/120,false);
            auto chosen=view;chosen.selection={2};
            WidgetCanvas canvas;WidgetBitmap bitmap;
            Check(canvas.Begin(420,392,dpi),"hover tile canvas begins");
            DrawOrganizer(canvas,420,392,sized,std::size_t{5},icon,chosen,&motion.Update(sized,sizedLayout,at,{},"",{},0,false));
            Check(canvas.Finish(bitmap),"hover tile canvas finishes");
            CaptureOne(directory,"organizer_grid_"+std::to_string(dpi)+"dpi_tile_"+std::to_string(scale),bitmap);
        }
        const OrganizerViewState edgeView;
        const auto edgeLayout=LayoutOrganizer(420,392,collection,edgeView);
        const auto first=edgeLayout.cells.front().bounds;
        const OrganizerPoint firstCenter{first.x+(list?21.f:41.f),first.y+(list?21.f:30.5f)};
        OrganizerMotion edgeMotion;
        edgeMotion.Update(collection,edgeLayout,firstCenter,{},"",{},.016f,false);
        for(unsigned i=0;i<180&&edgeMotion.Active();++i)edgeMotion.Update(collection,edgeLayout,firstCenter,{},"",{},1.f/120,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_hover_first_row",
            Paint(collection,edgeView,&edgeMotion.Update(collection,edgeLayout,firstCenter,{},"",{},0,false),dpi,icon));
        OrganizerMotion dragMotion;
        const std::vector<std::string> dragged{"item-1","item-3","item-5"};
        const auto dragPointer=OrganizerPoint{layout.viewport.x+layout.viewport.width*.60f,layout.viewport.y+layout.viewport.height*.52f};
        auto preview=view;preview.selection={1,3,5};
        for(std::size_t i=0;i<collection.items.size();++i)if(i!=1&&i!=3&&i!=5)preview.order.push_back(i);
        preview.order.insert(preview.order.begin()+3,{1,3,5});
        const auto previewLayout=LayoutOrganizer(420,392,collection,preview);
        dragMotion.Update(collection,layout,{}, {},"",{},0,false);
        for(unsigned i=0;i<3;++i)dragMotion.Update(collection,previewLayout,dragPointer,dragged,"item-1",{13,15},1.f/60,false);
        const auto dragFrame=dragMotion.Update(collection,previewLayout,dragPointer,dragged,"item-1",{13,15},0,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_drag_group_middle",Paint(collection,preview,&dragFrame,dpi,icon));
        const auto cancelFrame=dragMotion.Update(collection,layout,{}, {},"",{},0,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_cancel",Paint(collection,view,&cancelFrame,dpi,icon));
        for(unsigned i=0;i<180&&dragMotion.Active();++i)dragMotion.Update(collection,layout,{}, {},"",{},1.f/120,false);
        const auto settledFrame=dragMotion.Update(collection,layout,{}, {},"",{},0,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_settled",Paint(collection,view,&settledFrame,dpi,icon));
        auto externalCollection=collection;externalCollection.items.resize(4);OrganizerMotion externalMotion;
        const OrganizerViewState externalView;
        auto externalLayout=LayoutOrganizer(420,392,externalCollection,externalView);
        externalMotion.Update(externalCollection,externalLayout,{}, {},"",{},0,false);
        const auto externalPointer=OrganizerPoint{externalLayout.viewport.x+externalLayout.viewport.width*.52f,
            externalLayout.viewport.y+externalLayout.viewport.height*.45f};
        externalMotion.Update(externalCollection,externalLayout,externalPointer,{},"",{},.016f,false,true,1);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_external",
            Paint(externalCollection,externalView,&externalMotion.Update(externalCollection,externalLayout,externalPointer,{},"",{},.05f,false,true,1),dpi,icon));
        externalCollection.items.push_back({"capture-new","C:\\capture-new","New item"});
        externalLayout=LayoutOrganizer(420,392,externalCollection,externalView);
        for(unsigned frame=0;frame<3;++frame){
            const auto& externalFrame=externalMotion.Update(externalCollection,externalLayout,frame==0?std::optional<OrganizerPoint>{externalPointer}:std::nullopt,
                {},"",{},frame?1.f/30:0.f,false,frame==0,1);
            CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_external_newitem_"+std::to_string(frame),
                Paint(externalCollection,externalView,&externalFrame,dpi,icon));
        }
        for(unsigned i=0;i<360&&externalMotion.Active();++i)externalMotion.Update(externalCollection,externalLayout,{}, {},"",{},1.f/120,false);
        const auto& landed=externalMotion.Update(externalCollection,externalLayout,{}, {},"",{},0,false);
        CaptureOne(directory,std::string("organizer_")+mode+"_"+std::to_string(dpi)+"dpi_external_newitem_settled",
            Paint(externalCollection,externalView,&landed,dpi,icon));
    }
}
void LayoutAndSelection(){
    const auto collection=Collection();const auto original=LayoutOrganizer(420,392,collection);
    OrganizerViewState view;
    for(std::size_t i=collection.items.size();i>0;--i)view.order.push_back(i-1);
    const auto preview=LayoutOrganizer(420,392,collection,view);
    Check(preview.cells.front().index==19&&preview.cells.back().index==0,"complete preview order controls layout");
    Check(Same(preview.cells.front().bounds,original.cells.front().bounds),"preview preserves original geometry");
    const auto r=preview.cells.front().bounds;
    const auto hit=HitOrganizer(420,392,collection,r.x+r.width/2,r.y+r.height/2,view);
    Check(hit.kind==OrganizerHit::Kind::Item&&hit.index==19,"hit testing follows preview indices without changing geometry");
    view.filtered=true;view.visible={2,5};
    const auto filtered=LayoutOrganizer(420,392,collection,view);
    Check(filtered.cells.size()==2&&filtered.cells.front().index==2,"filtered results override preview order");
    view={};const auto base=Paint(collection,view,nullptr);view.selection={1,3};
    const auto selected=Paint(collection,view,nullptr);
    Check(Difference(base,selected,original.cells[2].bounds,96)==0,"multi-selection does not highlight unselected cells");
    const auto r1=original.cells[1].bounds;
    const unsigned x=static_cast<unsigned>(r1.x+4),y=static_cast<unsigned>(r1.y+r1.height/2);
    const auto edge=selected.pixels[static_cast<std::size_t>(y)*selected.width+x];
    // As on the Windows desktop, a selected icon sits on a tile covering its cell.
    Check(edge!=base.pixels[static_cast<std::size_t>(y)*base.width+x],
        "selection adds a desktop-style tile across the cell");
    view={};view.selected=1;const auto focused=Paint(collection,view,nullptr);
    Check(focused.pixels[static_cast<std::size_t>(y)*focused.width+x]==edge,
        "keyboard focus shows the same tile as selection");
    WidgetBitmap hovered;
    {WidgetCanvas canvas;Check(canvas.Begin(420,392,96),"hover canvas begins");
     DrawOrganizer(canvas,420,392,collection,std::size_t{1},{},{},nullptr);Check(canvas.Finish(hovered),"hover canvas finishes");}
    const auto tile=hovered.pixels[static_cast<std::size_t>(y)*hovered.width+x];
    Check(tile!=base.pixels[static_cast<std::size_t>(y)*base.width+x]&&tile!=edge,
        "hover shows a lighter tile than selection");
    view.selected.reset();Check(Paint(collection,view,nullptr).pixels==base.pixels,
        "clearing the last selection and focus restores the unselected pixels exactly");
}
void Settle(OrganizerMotion& motion,const lume::organizer::Collection& collection,const OrganizerLayout& layout,
    const std::optional<OrganizerPoint>& pointer={}){
    for(unsigned i=0;i<360&&motion.Active();++i)motion.Update(collection,layout,pointer,{},"",{},1.f/120,false);
    Check(!motion.Active(),"finite animation settles and stops requesting frames");
    const auto stopped=motion.Update(collection,layout,pointer,{},"",{},0,false);
    for(unsigned i=0;i<60;++i){
        const auto& later=motion.Update(collection,layout,pointer,{},"",{},1.f/60,false);
        Check(!motion.Active()&&later.cells.size()==stopped.cells.size()&&later.count==stopped.count&&
            later.external==stopped.external&&later.feedback==stopped.feedback&&later.slots.size()==stopped.slots.size(),
            "inactive frame metadata remains exactly unchanged for another second");
        for(std::size_t j=0;j<later.cells.size();++j){
            const auto& a=later.cells[j];const auto& b=stopped.cells[j];
            Check(a.index==b.index&&a.bounds.x==b.bounds.x&&a.bounds.y==b.bounds.y&&
                a.bounds.width==b.bounds.width&&a.bounds.height==b.bounds.height&&a.scale==b.scale&&
                a.lift==b.lift&&a.floating==b.floating,"inactive springs cannot hide subsequent coordinate changes");
        }
    }
}
void PointerAndDrag(){
    const auto collection=Collection();const auto layout=LayoutOrganizer(420,392,collection);
    OrganizerMotion motion;
    Check(motion.Update(collection,layout,{}, {},"",{},0,false).cells.size()==20&&!motion.Active(),"initial frame is settled");
    const OrganizerPoint offset{17.5f,24.25f};
    const std::vector<std::string> dragged{"item-0","item-2","item-3","item-5","item-7"};
    for(const float dt:{0.f,1.f/240,1.f/30,2.f}){
        const OrganizerPoint pointer{184.75f+dt,207.5f-dt};
        const auto& frame=motion.Update(collection,layout,pointer,dragged,"item-2",offset,dt,false);
        const auto main=Cell(frame,2);
        Check(Near(main.bounds.x,pointer.x-offset.x)&&Near(main.bounds.y,pointer.y-offset.y),"primary drag is exactly pointer minus offset, even at dt zero");
        Check(main.floating&&frame.count==5&&frame.slots.size()==5,"group drag reports total count and all destination slots");
        Check(std::count_if(frame.cells.begin(),frame.cells.end(),[](const auto& cell){return cell.floating;})==3,"drag draws at most primary plus two stacked companions");
        Check(frame.cells.back().index==2,"primary is above stacked companions");
        Check(Same(Cell(frame,1).bounds,layout.cells[1].bounds),"dragging never changes stationary layout targets");
    }
    for(unsigned i=0;i<360;++i)motion.Update(collection,layout,OrganizerPoint{190,210},dragged,"item-2",offset,1.f/120,false);
    Check(!motion.Active(),"holding a stationary drag does not spin an animation timer");
    const auto held=Cell(motion.Update(collection,layout,OrganizerPoint{190,210},dragged,"item-2",offset,0,false),2).bounds;
    const auto released=Cell(motion.Update(collection,layout,{}, {},"",{},0,false),2).bounds;
    Check(Same(held,released)&&motion.Active(),"cancel starts at current drag coordinate without jumping");
    Settle(motion,collection,layout);
    Check(Same(Cell(motion.Update(collection,layout,{}, {},"",{},0,false),2).bounds,layout.cells[2].bounds),"cancel returns to original target");
    const auto& invalid=motion.Update(collection,layout,OrganizerPoint{100,120},{"missing","item-1","item-1"},"missing",{},0,false);
    Check(invalid.count==1&&invalid.slots.size()==1&&Cell(invalid,1).floating,"stale and repeated drag IDs cannot duplicate cells or count");
}
void SpringsAndIdentity(){
    auto collection=Collection();const auto original=LayoutOrganizer(420,392,collection);
    auto target=original;target.cells[0].bounds.x+=120;
    OrganizerMotion motion;motion.Update(collection,original,{}, {},"",{},0,false);
    const auto first=Cell(motion.Update(collection,target,{}, {},"",{},.02f,false),0).bounds;
    Check(first.x>original.cells[0].bounds.x&&first.x<target.cells[0].bounds.x,"displacement approaches target elastically");
    const auto second=Cell(motion.Update(collection,target,{}, {},"",{},.02f,false),0).bounds;
    const auto interrupted=Cell(motion.Update(collection,original,{}, {},"",{},0,false),0).bounds;
    Check(Same(second,interrupted),"retarget at zero dt preserves current coordinates");
    const auto continuing=Cell(motion.Update(collection,original,{}, {},"",{},.001f,false),0).bounds;
    Check(continuing.x>second.x,"interrupted spring retains forward velocity before reversing");
    Settle(motion,collection,original);
    OrganizerMotion whole,split;
    whole.Update(collection,original,{}, {},"",{},0,false);split.Update(collection,original,{}, {},"",{},0,false);
    const auto one=Cell(whole.Update(collection,target,{}, {},"",{},.04f,false),0).bounds;
    split.Update(collection,target,{}, {},"",{},.02f,false);
    const auto two=Cell(split.Update(collection,target,{}, {},"",{},.02f,false),0).bounds;
    Check(Same(one,two),"closed-form spring is invariant to time subdivision");
    const auto before=Cell(split.Update(collection,target,{}, {},"",{},0,false),0).bounds;
    for(const auto dt:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        Check(Same(before,Cell(split.Update(collection,target,{}, {},"",{},dt,false),0).bounds),"invalid elapsed time cannot advance or poison animation");
    OrganizerMotion boundedA,boundedB;
    boundedA.Update(collection,original,{}, {},"",{},0,false);boundedB.Update(collection,original,{}, {},"",{},0,false);
    Check(Same(Cell(boundedA.Update(collection,target,{}, {},"",{},1000,false),0).bounds,
        Cell(boundedB.Update(collection,target,{}, {},"",{},.05f,false),0).bounds),"elapsed time is bounded after a stalled UI thread");
    const OrganizerPoint pointer{220,160},offset{12,18};
    motion.Update(collection,original,pointer,{"item-0"},"item-0",offset,.016f,false);
    const auto held=Cell(motion.Update(collection,original,pointer,{"item-0"},"item-0",offset,.016f,false),0).bounds;
    std::swap(collection.items[0],collection.items[5]);
    const auto reordered=LayoutOrganizer(420,392,collection);
    Check(Same(Cell(motion.Update(collection,reordered,{}, {},"",{},0,false),5).bounds,held),"drop retains coordinates by stable ID after model indices change");
    Settle(motion,collection,reordered);
    Check(Same(Cell(motion.Update(collection,reordered,{}, {},"",{},0,false),5).bounds,reordered.cells[5].bounds),"drop settles at new committed order");
    auto preview=reordered;std::swap(preview.cells[0].bounds,preview.cells[1].bounds);
    motion.Update(collection,preview,{}, {},"",{},.02f,false);
    Check(motion.Active(),"live preview displaces remaining items");
    motion.Reset();Check(!motion.Active(),"reset clears pending work");
    Check(Same(Cell(motion.Update(collection,reordered,{}, {},"",{},0,false),0).bounds,reordered.cells[0].bounds),"reset leaves no old coordinates or velocity");
    motion.Update(collection,preview,{}, {},"",{},.02f,false);
    collection.id="different";
    Check(Same(Cell(motion.Update(collection,reordered,{}, {},"",{},0,false),0).bounds,reordered.cells[0].bounds),"different collections cannot inherit motion through reused item IDs");
    collection.items.clear();const auto empty=LayoutOrganizer(420,392,collection);
    Check(motion.Update(collection,empty,{}, {},"",{},0,false).cells.empty()&&!motion.Active(),"removing all items releases their pending springs");
    collection=Collection();
    Check(Same(Cell(motion.Update(collection,original,{}, {},"",{},0,false),0).bounds,original.cells[0].bounds),"new items start at their layout target");
}
void HoverAndReduced(){
    auto collection=Collection();const auto layout=LayoutOrganizer(420,392,collection);
    const auto r=layout.cells[1].bounds;const OrganizerPoint center{r.x+41,r.y+30.5f};
    OrganizerMotion motion;motion.Update(collection,layout,{}, {},"",{},0,false);
    motion.Update(collection,layout,center,{},"",{},.016f,false);Settle(motion,collection,layout,center);
    const auto frame=motion.Update(collection,layout,center,{},"",{},0,false);
    Check(Cell(frame,1).scale>=1.25f&&Cell(frame,1).scale<=1.30f&&Near(Cell(frame,1).lift,5),"hover center scales prominently and lifts five DIP");
    Check(Cell(frame,0).scale>1&&Cell(frame,0).scale<Cell(frame,1).scale,"neighbor magnification falls off with distance");
    Check(Same(Cell(frame,1).bounds,r),"hover leaves cell geometry untouched");
    for(unsigned i=0;i<300;++i)motion.Update(collection,layout,OrganizerPoint{center.x+.1f,center.y}, {},"",{},1.f/120,false);
    const auto nearby=motion.Update(collection,layout,OrganizerPoint{center.x+.1f,center.y}, {},"",{},0,false);
    Check(std::abs(Cell(nearby,1).scale-Cell(frame,1).scale)<.002f,"distance response is continuous for sub-DIP pointer motion");
    auto target=layout;target.cells[1].bounds.x+=100;
    const auto reduced=motion.Update(collection,target,center,{},"",{},.016f,true);
    Check(!motion.Active()&&Same(Cell(reduced,1).bounds,target.cells[1].bounds)&&
        Cell(reduced,1).scale==1&&Cell(reduced,1).lift==0,"reduced motion immediately snaps position and disables hover springs");
    const auto direct=motion.Update(collection,layout,center,{"item-1"},"item-1",{10,12},0,true);
    Check(Near(Cell(direct,1).bounds.x,center.x-10)&&!motion.Active(),"reduced motion retains direct pointer tracking");
    const auto external=motion.Update(collection,layout,center,{},"",{},0,true,true,12);
    Check(external.external&&external.count==12&&external.feedback==1&&!motion.Active(),"external feedback is static in reduced motion");
    motion.Update(collection,layout,{}, {},"",{},.016f,false,false,0);Settle(motion,collection,layout);
    const auto cleared=motion.Update(collection,layout,{}, {},"",{},0,false);
    Check(!cleared.external&&cleared.count==0&&cleared.feedback==0,"external leave eventually clears feedback and badge");
}
void ExternalArrival(){
    for(const bool beforeLeave:{false,true}){
        auto collection=Collection();collection.items.resize(4);
        const auto original=LayoutOrganizer(420,392,collection);OrganizerMotion motion;
        motion.Update(collection,original,{}, {},"",{},0,false);
        motion.Update(collection,original,OrganizerPoint{-800,1400}, {},"",{},.016f,false,true,2);
        collection.items.push_back({"added-a","C:\\a","Added A"});
        collection.items.push_back({"added-b","C:\\b","Added B"});
        const auto layout=LayoutOrganizer(420,392,collection);
        const auto entered=motion.Update(collection,layout,beforeLeave?std::optional<OrganizerPoint>{{-800,1400}}:std::nullopt,
            {},"",{},0,false,beforeLeave,2);
        const auto fresh=Cell(entered,4);const auto target=layout.cells[4].bounds;
        Check(fresh.scale>=.89f&&fresh.scale<=.91f&&motion.Active(),"new external IDs begin a short 0.9 scale arrival before or immediately after OLE Leave");
        const auto distance=std::hypot(fresh.bounds.x-target.x,fresh.bounds.y-target.y);
        Check(distance>1&&distance<=48.01f,"external arrival starts near its target even for a far outside pointer");
        Check(Cell(entered,0).scale==1,"external drop never replays entrance for existing IDs");
        const auto next=Cell(motion.Update(collection,layout,{}, {},"",{},.016f,false),4);
        Check(next.scale>fresh.scale&&next.scale<1&&
            std::hypot(next.bounds.x-target.x,next.bounds.y-target.y)<distance,"new external item has a visible intermediate frame");
        Settle(motion,collection,layout);
        const auto settled=Cell(motion.Update(collection,layout,{}, {},"",{},0,false),4);
        Check(Same(settled.bounds,target)&&settled.scale==1&&!motion.Active(),"arrival finishes at target and does not keep idle animation alive");
    }
    auto collection=Collection();collection.items.resize(4);OrganizerMotion motion;
    auto layout=LayoutOrganizer(420,392,collection);
    motion.Update(collection,layout,{}, {},"",{},0,false);
    collection.items.push_back({"ordinary","C:\\ordinary","Ordinary"});layout=LayoutOrganizer(420,392,collection);
    const auto ordinary=Cell(motion.Update(collection,layout,{}, {},"",{},0,false),4);
    Check(Same(ordinary.bounds,layout.cells[4].bounds)&&ordinary.scale==1,"ordinary new IDs do not trigger external entrance");
    motion.Update(collection,layout,OrganizerPoint{200,180}, {},"",{},.016f,false,true,1);
    collection.items.push_back({"reduced","C:\\reduced","Reduced"});layout=LayoutOrganizer(420,392,collection);
    const auto reduced=Cell(motion.Update(collection,layout,{}, {},"",{},0,true),5);
    Check(Same(reduced.bounds,layout.cells[5].bounds)&&reduced.scale==1&&!motion.Active(),"reduced motion skips external arrival and discards remaining velocity");
    motion.Reset();
    const auto initial=motion.Update(collection,layout,{}, {},"",{},0,false);
    Check(std::all_of(initial.cells.begin(),initial.cells.end(),[](const auto& cell){return cell.scale==1;})&&!motion.Active(),
        "first ordinary paint never animates the entire collection");
    OrganizerViewState filter;filter.filtered=true;filter.visible={0};
    motion.Update(collection,LayoutOrganizer(420,392,collection,filter),OrganizerPoint{180,190}, {},"",{},.05f,false,true,1);
    const auto revealed=motion.Update(collection,layout,OrganizerPoint{180,190}, {},"",{},0,false,true,1);
    Check(Cell(revealed,5).scale==1,"unfiltering an existing ID during external hover does not count as a new item");
}
void HoverEdges(){
    auto collection=Collection();
    const auto magenta=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xffff00ffu}});
    const auto transparent=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0}});
    const auto ink=[](const WidgetBitmap& bitmap){
        return std::count(bitmap.pixels.begin(),bitmap.pixels.end(),0xffff00ffu);
    };
    for(const bool list:{false,true})for(const unsigned dpi:{96u,120u,144u,192u}){
        collection.layout=list?lume::organizer::Layout::List:lume::organizer::Layout::Icons;
        const auto layout=LayoutOrganizer(420,392,collection);const auto viewport=layout.viewport;
        const auto targets=list?std::vector<std::size_t>{0,5}:std::vector<std::size_t>{0,3,8,11};
        for(const auto index:targets){
            const auto r=layout.cells[index].bounds;
            Check(r.x>=viewport.x&&r.y>=viewport.y&&r.x+r.width<=viewport.x+viewport.width&&
                r.y+r.height<=viewport.y+viewport.height,"edge hover fixture has a fully visible original cell");
            const OrganizerPoint center{r.x+(list?21.f:41.f),r.y+(list?21.f:30.5f)};
            OrganizerMotion motion;motion.Update(collection,layout,center,{},"",{},.016f,false);
            Settle(motion,collection,layout,center);
            const auto frame=motion.Update(collection,layout,center,{},"",{},0,false);
            const auto& cell=Cell(frame,index);
            Check(Near(cell.scale,1.28f)&&Near(cell.lift,5)&&Same(cell.bounds,r),
                "edge handling preserves maximum hover scale and original cell geometry");
            const OrganizerImage image=[&](const auto& item){return item.id==collection.items[index].id?magenta:transparent;};
            const auto actual=Paint(collection,{},&frame,dpi,image);
            const float side=(list?24.f:44.f)*cell.scale;
            const WidgetRect expected{std::clamp(center.x-side/2,viewport.x,viewport.x+viewport.width-side),
                std::clamp(center.y-cell.lift-side/2,viewport.y,viewport.y+viewport.height-side),side,side};
            WidgetCanvas canvas;WidgetBitmap reference;
            Check(canvas.Begin(420,392,dpi)&&canvas.ImageBitmap(magenta,expected)&&canvas.Finish(reference),
                "uncropped reference icon is drawn at full hover size without any clip");
            Check(ink(actual)==ink(reference),"fully visible edge cells retain every pixel of the maximum-size hover icon");
            CheckClip(actual,Paint(collection,{},nullptr,dpi,image),viewport,dpi);
        }
        OrganizerViewState partial;partial.scroll=17;
        const auto partialLayout=LayoutOrganizer(420,392,collection,partial);
        const auto r=partialLayout.cells.front().bounds;
        const OrganizerPoint center{r.x+(list?21.f:41.f),r.y+(list?21.f:30.5f)};
        OrganizerMotion motion;motion.Update(collection,partialLayout,center,{},"",{},.016f,false);
        Settle(motion,collection,partialLayout,center);
        const auto frame=motion.Update(collection,partialLayout,center,{},"",{},0,false);
        const OrganizerImage image=[&](const auto& item){return item.id==collection.items.front().id?magenta:transparent;};
        const auto actual=Paint(collection,partial,&frame,dpi,image);
        const auto& cell=Cell(frame,0);const float side=(list?24.f:44.f)*cell.scale;
        WidgetCanvas canvas;WidgetBitmap reference;
        Check(canvas.Begin(420,392,dpi)&&canvas.ImageBitmap(magenta,{center.x-side/2,150,side,side})&&canvas.Finish(reference),
            "full-size control for the partially clipped row");
        Check(ink(actual)>0&&ink(actual)<ink(reference),"partially visible rows remain clipped instead of pinning icons back into viewport");
        CheckClip(actual,Paint(collection,partial,nullptr,dpi,image),partialLayout.viewport,dpi);
    }
}
void Pixels(){
    auto collection=Collection();
    const auto magenta=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xffff00ffu}});
    const auto green=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xff00ff00u}});
    const OrganizerImage icon=[&](const auto& item){return item.id=="item-1"?green:magenta;};
    for(const bool list:{false,true})for(const unsigned dpi:{96u,120u,144u,192u}){
        collection.layout=list?lume::organizer::Layout::List:lume::organizer::Layout::Icons;
        OrganizerViewState view;view.scroll=list?17.f:47.f;
        const auto layout=LayoutOrganizer(420,392,collection,view);const auto viewport=layout.viewport;
        OrganizerMotion motion;
        auto frame=motion.Update(collection,layout,{}, {},"",{},0,false);
        const auto base=Paint(collection,view,nullptr,dpi,icon);
        Check(base.pixels==Paint(collection,view,&frame,dpi,icon).pixels,"neutral motion preserves existing canvas pixels");
        const auto r=layout.cells[list?2:5].bounds;
        const OrganizerPoint pointer{r.x+(list?21.f:41.f),r.y+(list?21.f:30.5f)};
        motion.Update(collection,layout,pointer,{},"",{},.016f,false);Settle(motion,collection,layout,pointer);
        frame=motion.Update(collection,layout,pointer,{},"",{},0,false);
        const auto magnified=Paint(collection,view,&frame,dpi,icon);
        Check(Difference(base,magnified,r,dpi)>50,"hover changes real grid/list icon pixels at every DPI");
        const WidgetRect label=list?WidgetRect{r.x+45,r.y+10,r.width-76,22}:WidgetRect{r.x+5,r.y+58.5f,r.width-10,16};
        Check(Difference(base,magnified,label,dpi)==0,"hover scaling and lift never move or collide with labels");
        CheckClip(magnified,base,viewport,dpi);
        view.selection={2,5};
        const auto reduced=motion.Update(collection,layout,pointer,{},"",{},0,true);
        Check(Paint(collection,view,&reduced,dpi,icon).pixels==Paint(collection,view,nullptr,dpi,icon).pixels,"reduced motion keeps static multi-selection pixels");
        view.selection.clear();
        frame=motion.Update(collection,layout,OrganizerPoint{viewport.x-22,viewport.y-20},{"item-0","item-1","item-2","item-3"},"item-0",{},.03f,false);
        frame.marquee=WidgetRect{viewport.x-50,viewport.y-50,viewport.width+100,viewport.height+100};
        const auto dragged=Paint(collection,view,&frame,dpi,icon);
        Check(Difference(base,dragged,viewport,dpi)>100,"drag stack, slots and marquee affect real canvas pixels");CheckClip(dragged,base,viewport,dpi);
        auto badge=frame;badge.count=1;
        Check(Paint(collection,view,&badge,dpi,icon).pixels!=dragged.pixels,"multi-drag count badge is visible");
        auto renamed=collection;renamed.items[1].name="Companion name must not overlap";renamed.items[2].name="Another companion";
        Check(Paint(renamed,view,&frame,dpi,icon).pixels==dragged.pixels,"drag companions stack only icons and never overlap names");
        auto returning=frame;returning.count=0;
        Check(Paint(renamed,view,&returning,dpi,icon).pixels==Paint(collection,view,&returning,dpi,icon).pixels,
            "companions keep names hidden after count clears while floating back to their targets");
        motion.Reset();frame=motion.Update(collection,layout,{}, {},"",{},0,true,true,7);
        const auto external=Paint(collection,view,&frame,dpi,icon);
        Check(Difference(base,external,viewport,dpi)>100,"external drop highlights viewport");CheckClip(external,base,viewport,dpi);
        const unsigned sampleX=static_cast<unsigned>((viewport.x+1)*dpi/96);
        const unsigned sampleY=static_cast<unsigned>((viewport.y+viewport.height/2)*dpi/96);
        const auto tint=external.pixels[static_cast<std::size_t>(sampleY)*external.width+sampleX];
        Check((tint&255)>((tint>>16)&255)&&((tint>>8)&255)>((tint>>16)&255),
            "external feedback uses a cool blue tint with actual colored canvas pixels");
        frame.count=1;Check(Paint(collection,view,&frame,dpi,icon).pixels!=external.pixels,"external drop renders its count badge");
        frame={};const auto original=layout.cells[list?2:5].bounds;
        frame.cells.push_back({0,original,1,0,true});
        frame.cells.push_back({1,original,1,0,false});
        const auto overlap=Paint(collection,view,&frame,dpi,icon);
        const unsigned x=static_cast<unsigned>((original.x+(list?21.f:41.f))*dpi/96);
        const unsigned y=static_cast<unsigned>((original.y+(list?21.f:30.5f))*dpi/96);
        Check(overlap.pixels[static_cast<std::size_t>(y)*overlap.width+x]==0xffff00ffu,"floating layer is last even when frame cells are not sorted");
        frame.cells={{0,original,1,0,false}};
        const auto before=Paint(collection,view,&frame,dpi,icon);frame.cells[0].bounds.y+=25;
        const auto moved=Paint(collection,view,&frame,dpi,icon);
        const WidgetRect oldLabel=list?WidgetRect{original.x+45,original.y+10,original.width-76,22}:
            WidgetRect{original.x+5,original.y+58.5f,original.width-10,16};
        Check(Difference(before,moved,oldLabel,dpi)>10,"item name follows animated cell bounds");
        auto noIcons=Paint(collection,view,&frame,dpi);
        Check(Difference(noIcons,moved,viewport,dpi)>50,"missing cached icon falls back to existing glyph drawing");
    }
    collection.mode=lume::organizer::Mode::Collapsed;
    const auto layout=LayoutOrganizer(180,216,collection);OrganizerMotion motion;
    const auto frame=motion.Update(collection,layout,OrganizerPoint{layout.cells[0].bounds.x+26,layout.cells[0].bounds.y+26}, {},"",{},0,true);
    Check(frame.cells.size()==4&&!motion.Active(),"collapsed layout retains four static cells under reduced motion");
}
}
int main(int argc,char** argv){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initializes for real Direct2D canvas tests");
    const bool capture=argc==3&&std::string(argv[1])=="--capture";
    Check(argc==1||capture,"usage: organizer_motion_test [--capture <directory>]");
    Check(CompositeOver(0,0xff204060u)==0xff204060u&&
        CompositeOver(0x80800000u,0xff204060u)==0xff902030u&&
        CompositeOver(0xff12ab34u,0xff204060u)==0xff12ab34u,
        "BMP compositing respects premultiplied transparent, half-alpha and opaque BGRA pixels");
    LayoutAndSelection();
    PointerAndDrag();SpringsAndIdentity();HoverAndReduced();ExternalArrival();HoverEdges();Pixels();TabStrip();
    Check(VerifyOrganizerViewport(),"existing viewport pixels and hit tests pass without motion");
    if(capture)CaptureVisuals(std::filesystem::u8path(argv[2]));
    CoUninitialize();std::cout<<"PASS organizer motion, direct drag, interruption, settle, identity and real canvas pixels\n";
}
