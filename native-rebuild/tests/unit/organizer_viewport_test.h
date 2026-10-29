#pragma once
#include "platform/windows/organizer/organizer_view.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

// Pixel and hit tests exercise real production drawing with an opaque magenta
// icon. The two scroll positions cut both the first and last visible row.
inline bool VerifyOrganizerViewport(){
    using namespace lume::platform::windows;
    unsigned failures=0;
    const auto check=[&](bool value,const char* message,unsigned dpi,bool list){
        if(!value){++failures;std::cerr<<"viewport "<<(list?"list":"grid")<<" "<<dpi<<" DPI: "<<message<<'\n';}
    };
    auto collection=lume::organizer::Defaults().collections.front();
    collection.iconScale=100;
    for(unsigned i=0;i<20;++i)collection.items.push_back({"viewport-"+std::to_string(i),
        "C:\\LumeCapture\\"+std::to_string(i),u8"部分可见图标名称",lume::organizer::ItemKind::File});
    const auto icon=std::make_shared<const WidgetBitmap>(WidgetBitmap{1,1,{0xffff00ffu}});
    for(const bool list:{false,true})for(const unsigned dpi:{96u,120u,144u,192u}){
        collection.layout=list?lume::organizer::Layout::List:lume::organizer::Layout::Icons;
        OrganizerViewState view;view.scroll=list?17.f:47.f;view.selected=0;
        const auto layout=LayoutOrganizer(420,392,collection,view);
        const auto viewport=layout.viewport;
        WidgetCanvas canvas;WidgetBitmap pixels,baseline;
        check(canvas.Begin(420,392,dpi),"canvas begins",dpi,list);
        std::set<std::string> requested;
        DrawOrganizer(canvas,420,392,collection,{},[&](const lume::organizer::Item& item){
            requested.insert(item.id);return icon;
        },view);
        check(canvas.Finish(pixels),"drawing finishes",dpi,list);
        auto empty=collection;empty.items.clear();
        check(canvas.Begin(420,392,dpi),"baseline begins",dpi,list);
        DrawOrganizer(canvas,420,392,empty,{}, {},view);
        check(canvas.Finish(baseline),"baseline finishes",dpi,list);
        unsigned outsideChanged=0,topInk=0,bottomInk=0;
        const auto top=layout.cells.front();
        const auto bottom=layout.cells.at(list?6:12);
        for(unsigned y=0;y<pixels.height;++y)for(unsigned x=0;x<pixels.width;++x){
            const float dx=(static_cast<float>(x)+.5f)*96/dpi,dy=(static_cast<float>(y)+.5f)*96/dpi;
            const auto index=static_cast<std::size_t>(y)*pixels.width+x;
            const bool inside=dx>=viewport.x&&dx<viewport.x+viewport.width&&
                dy>=viewport.y&&dy<viewport.y+viewport.height;
            if(!inside&&pixels.pixels[index]!=baseline.pixels[index])++outsideChanged;
            if(pixels.pixels[index]!=0xffff00ffu)continue;
            if(dx>=top.bounds.x&&dx<top.bounds.x+top.bounds.width&&dy>=top.bounds.y&&dy<top.bounds.y+top.bounds.height)++topInk;
            if(dx>=bottom.bounds.x&&dx<bottom.bounds.x+bottom.bounds.width&&dy>=bottom.bounds.y&&dy<bottom.bounds.y+bottom.bounds.height)++bottomInk;
        }
        check(topInk>8,"partly visible top icon must remain drawn",dpi,list);
        check(bottomInk>8,"partly visible bottom icon must remain drawn",dpi,list);
        check(outsideChanged==0,"icons, labels and selection cannot escape viewport",dpi,list);
        for(const auto* cell:{&top,&bottom}){
            const float x=cell->bounds.x+cell->bounds.width/2;
            const float first=std::max(viewport.y,cell->bounds.y),last=std::min(viewport.y+viewport.height,cell->bounds.y+cell->bounds.height);
            const auto hit=HitOrganizer(420,392,collection,x,(first+last)/2,view);
            check(hit.kind==OrganizerHit::Kind::Item&&hit.index==cell->index,
                "visible part of clipped row must remain interactive",dpi,list);
        }
        const auto above=HitOrganizer(420,392,collection,top.bounds.x+top.bounds.width/2,viewport.y-1,view);
        const auto below=HitOrganizer(420,392,collection,bottom.bounds.x+bottom.bounds.width/2,viewport.y+viewport.height+1,view);
        check(above.kind==OrganizerHit::Kind::None&&below.kind==OrganizerHit::Kind::None,
            "clipped-off row area must not be interactive",dpi,list);
        bool hiddenRequested=false;
        for(const auto& cell:layout.cells)
            if(cell.bounds.y>=viewport.y+viewport.height||cell.bounds.y+cell.bounds.height<=viewport.y)
                hiddenRequested=hiddenRequested||requested.count(collection.items[cell.index].id)!=0;
        check(!hiddenRequested,"fully invisible rows skip icon lookup and drawing",dpi,list);
    }
    return failures==0;
}
