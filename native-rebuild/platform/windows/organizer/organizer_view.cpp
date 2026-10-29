#include "platform/windows/organizer/organizer_view.h"
#include "platform/windows/organizer/organizer_motion.h"
#include "platform/windows/organizer/organizer_text.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace lume::platform::windows {
namespace {
bool Contains(WidgetRect r,float x,float y){
    return r.width>0&&r.height>0&&x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;
}
bool Intersects(WidgetRect a,WidgetRect b){
    return a.width>0&&a.height>0&&b.width>0&&b.height>0&&
        a.x<b.x+b.width&&a.x+a.width>b.x&&a.y<b.y+b.height&&a.y+a.height>b.y;
}
std::vector<std::size_t> Visible(const organizer::Collection& collection,const OrganizerViewState& view){
    if(view.filtered)return view.visible;
    if(!view.order.empty())return view.order;
    std::vector<std::size_t> result(collection.items.size());
    std::iota(result.begin(),result.end(),0);
    return result;
}
void Outline(WidgetCanvas& canvas,WidgetRect r,float radius,float alpha,float thickness,std::uint32_t rgb=0xffffff){
    // Stroke the original SVG shape without filling its transparent interior.
    constexpr float pi=3.14159265358979323846f;
    const float cx[]={r.x+r.width-radius,r.x+r.width-radius,r.x+radius,r.x+radius};
    const float cy[]={r.y+radius,r.y+r.height-radius,r.y+r.height-radius,r.y+radius};
    float firstX=0,firstY=0,previousX=0,previousY=0;
    for(unsigned corner=0;corner<4;++corner)for(unsigned step=0;step<=12;++step){
        const float angle=(-.5f+static_cast<float>(corner)*.5f+static_cast<float>(step)/24)*pi;
        const float x=cx[corner]+std::cos(angle)*radius,y=cy[corner]+std::sin(angle)*radius;
        if(!corner&&!step){firstX=x;firstY=y;}
        else canvas.Line(previousX,previousY,x,y,rgb,alpha,thickness);
        previousX=x;previousY=y;
    }
    canvas.Line(previousX,previousY,firstX,firstY,rgb,alpha,thickness);
}
void SearchGlyph(WidgetCanvas& canvas,WidgetRect r){
    const float x=r.x+(r.width-16)/2,y=r.y+(r.height-16)/2;
    constexpr float scale=16.f/24,stroke=1.7f*scale;
    Outline(canvas,{x+4*scale,y+4*scale,13*scale,13*scale},6.5f*scale,.88f,stroke);
    canvas.Line(x+16*scale,y+16*scale,x+21*scale,y+21*scale,0xffffff,.88f,stroke);
    for(const float end:{16.f,21.f})
        canvas.Rounded({x+end*scale-stroke/2,y+end*scale-stroke/2,stroke,stroke},stroke/2,0xffffff,.88f);
}
void Name(WidgetCanvas& canvas,const std::wstring& text,WidgetRect r,float size,WidgetTextAlign align){
    if(canvas.MeasureText(text,size)<=r.width){canvas.Text(text,r,size,0xffffff,1,false,align);return;}
    auto shortened=text;
    while(!shortened.empty()&&canvas.MeasureText(shortened+L"\u2026",size)>r.width){
        const auto last=shortened.back();shortened.pop_back();
        if(last>=0xdc00&&last<=0xdfff&&!shortened.empty()&&shortened.back()>=0xd800&&shortened.back()<=0xdbff)
            shortened.pop_back();
    }
    canvas.Text(shortened+L"\u2026",r,size);
}
void Fallback(WidgetCanvas& canvas,WidgetRect r,organizer::ItemKind kind){
    const auto rgb=kind==organizer::ItemKind::Folder?0xe9c366u:0xe6edf9u;
    canvas.Rounded({r.x+r.width*.16f,r.y+r.height*.08f,r.width*.68f,r.height*.84f},r.width*.14f,rgb,.95f);
    if(kind!=organizer::ItemKind::Folder)for(unsigned row=0;row<3;++row)
        canvas.Line(r.x+r.width*.32f,r.y+r.height*(.37f+row*.15f),
            r.x+r.width*(row==2?.55f:.68f),r.y+r.height*(.37f+row*.15f),0x8b9aba,.95f,1.5f);
}
WidgetRect IconBounds(WidgetRect r,bool collapsed,bool list,float scale=1,float lift=0,const WidgetRect* viewport=nullptr,float iconScale=1){
    WidgetRect icon=collapsed?WidgetRect{r.x+6,r.y+6,40,40}:
        list?WidgetRect{r.x+9,r.y+(r.height-24*iconScale)/2,24*iconScale,24*iconScale}:
        WidgetRect{r.x+(r.width-44*iconScale)/2,r.y+8.5f*iconScale,44*iconScale,44*iconScale};
    const float growth=icon.width*(scale-1);
    icon.x-=growth/2;icon.y-=growth/2+lift;icon.width+=growth;icon.height+=growth;
    // 整格可见时只约束图标绘制范围；部分行和浮动拖动项仍按真实位置裁切。
    if(viewport&&(scale!=1||lift!=0)&&r.x>=viewport->x&&r.y>=viewport->y&&
        r.x+r.width<=viewport->x+viewport->width&&r.y+r.height<=viewport->y+viewport->height&&
        icon.width<=viewport->width&&icon.height<=viewport->height){
        icon.x=std::clamp(icon.x,viewport->x,viewport->x+viewport->width-icon.width);
        icon.y=std::clamp(icon.y,viewport->y,viewport->y+viewport->height-icon.height);
    }
    return icon;
}
void CountBadge(WidgetCanvas& canvas,WidgetRect icon,std::size_t count,WidgetRect clip){
    if(!count||clip.width<24||clip.height<20)return;
    const auto text=std::to_wstring(count);
    const float width=std::min(clip.width,std::max(24.f,canvas.MeasureText(text,11,WidgetTextWeight::Semibold)+12));
    const WidgetRect badge{std::clamp(icon.x+icon.width-10,clip.x,clip.x+clip.width-width),
        std::clamp(icon.y+icon.height-12,clip.y,clip.y+clip.height-20),width,20};
    canvas.Rounded(badge,7,0x246de0,.98f);
    Outline(canvas,{badge.x+.5f,badge.y+.5f,badge.width-1,badge.height-1},6.5f,.65f,1);
    canvas.Text(text,badge,11,0xffffff,1,WidgetTextWeight::Semibold,WidgetTextAlign::Center);
}
}
// Layout has no canvas; estimate the 13px tab label width by script so hit
// rectangles stay stable. Drawing truncates with an ellipsis if it overflows.
float OrganizerTabWidth(const std::wstring& name){
    float text=0;
    for(const auto c:name){
        if(c>=0xdc00&&c<=0xdfff)continue;
        text+=c>=0x2e80?13.f:(c==L' '?3.6f:(c>=L'A'&&c<=L'Z')?8.4f:7.f);
    }
    return std::clamp(text+24,56.f,140.f);
}
OrganizerLayout LayoutOrganizer(float width,float height,const organizer::Collection& collection,const OrganizerViewState& view){
    OrganizerLayout result;
    result.collapsed=collection.mode==organizer::Mode::Collapsed&&!view.temporaryExpanded;
    const auto indices=Visible(collection,view);
    if(result.collapsed){
        result.title={std::max(0.f,(width-160)/2),height-33,std::min(160.f,width),30};
        result.search={width-31,7,24,24};
        result.viewport={24,24,width-48,height-84};
        const float left=(width-116)/2,top=(height-36-116)/2;
        for(std::size_t i=0;i<std::min<std::size_t>(4,indices.size());++i)
            result.cells.push_back({indices[i],{left+static_cast<float>(i%2)*64,top+static_cast<float>(i/2)*64,52,52}});
        return result;
    }
    result.title={49,21,std::min(120.f,std::max(0.f,width-115)),30};
    result.search={width-49,22,28,28};
    if(width<200){result.title={12,21,std::max(0.f,width-64),30};result.search={width-38,22,26,28};}
    result.header={0,0,width,58};
    if(view.tabs.size()>1){
        // 标签替代标题：位于网格图标与搜索之间，超出宽度时横向滚动。
        result.tabStrip={46,21,std::max(0.f,width-46-55),30};
        if(width<200)result.tabStrip={12,21,std::max(0.f,width-60),30};
        float x=0;
        for(const auto& name:view.tabs){
            const float w=OrganizerTabWidth(name);
            result.tabs.push_back({result.tabStrip.x+x,result.tabStrip.y,w,30});x+=w+4;
        }
        result.maxTabScroll=std::max(0.f,x-4-result.tabStrip.width);
        const float scroll=std::clamp(view.tabScroll,0.f,result.maxTabScroll);
        for(auto& tab:result.tabs)tab.x-=scroll;
        result.title={};
        if(view.activeTab<result.tabs.size())result.title=result.tabs[view.activeTab];
    }
    if(view.searchOpen){
        result.searchInput={28,63,std::max(0.f,width-56),30};
    }
    const float top=view.searchOpen?105.f:67.f;
    result.viewport={21,top-2,std::max(0.f,width-42),std::max(0.f,height-(top-2)-(view.searchOpen?47.f:51.f))};
    const bool list=collection.layout==organizer::Layout::List;
    const float iconScale=static_cast<float>(std::clamp(collection.iconScale,50,120))/100;
    const float cellWidth=list?result.viewport.width:std::min(result.viewport.width,82.f*iconScale);
    const float cellHeight=list?std::max(42.f,42.f*iconScale):std::max(60.f,82.f*iconScale),gap=list?4.f:8.f;
    const std::size_t columns=list?1:std::max<std::size_t>(1,std::min<std::size_t>(indices.size(),
        static_cast<std::size_t>(std::max(1.f,std::floor((result.viewport.width+gap)/(cellWidth+gap))))));
    const auto rows=(indices.size()+columns-1)/columns;
    const float contentHeight=rows?static_cast<float>(rows)*(cellHeight+gap)-gap:0;
    result.maxScroll=std::max(0.f,contentHeight+6-result.viewport.height);
    const float scroll=std::clamp(view.scroll,0.f,result.maxScroll);
    const float spare=list?0:std::max(0.f,result.viewport.width-static_cast<float>(columns)*cellWidth-static_cast<float>(columns-1)*gap);
    const float around=spare/static_cast<float>(columns+1);
    for(std::size_t i=0;i<indices.size();++i){
        const float x=result.viewport.x+around+static_cast<float>(i%columns)*(cellWidth+gap+around);
        const float y=top+static_cast<float>(i/columns)*(cellHeight+gap)-scroll;
        const WidgetRect cell{x,y,cellWidth,cellHeight};
        result.cells.push_back({indices[i],cell});
    }
    return result;
}
std::vector<LumeMaterialPanel> OrganizerSearchPanels(const OrganizerLayout& layout,const OrganizerViewState& view){
    const auto button=layout.search;
    std::vector<LumeMaterialPanel> panels{{button.x,button.y,button.width,button.height,-1}};
    if(view.searchOpen&&!layout.collapsed){
        const auto input=layout.searchInput;
        const float height=input.height+4;
        panels.push_back({input.x-6,input.y-2,input.width+12,height,height/2});
    }
    return panels;
}
void DrawOrganizer(WidgetCanvas& canvas,float width,float height,const organizer::Collection& collection,
    std::optional<std::size_t> hover,const OrganizerImage& image,const OrganizerViewState& view,const OrganizerMotionFrame* motion){
    const auto layout=LayoutOrganizer(width,height,collection,view);
    if(view.mergeTarget){
        // 另一个收纳拖到这里松手即合并为标签。
        canvas.Rounded({8,8,width-16,height-16},18,0x8bc8ff,.10f);
        Outline(canvas,{8.5f,8.5f,width-17,height-17},17.5f,.85f,1.6f);
    }
    if(!layout.tabs.empty()){
        // Renaming covers the whole strip with a translucent editor.
        if(!view.titleEditing&&canvas.PushClip(layout.tabStrip)){
            for(std::size_t i=0;i<layout.tabs.size()&&i<view.tabs.size();++i){
                const auto r=layout.tabs[i];
                const bool active=i==view.activeTab,lifted=view.liftedTab&&*view.liftedTab==i;
                const bool hot=(view.hoverTab&&*view.hoverTab==i)||(view.dropTab&&*view.dropTab==i);
                if(lifted)canvas.Rounded(r,9,0x8bc8ff,.30f);
                else if(active||hot)canvas.Rounded(r,9,0xffffff,active?.16f:.08f);
                if(lifted||(view.dropTab&&*view.dropTab==i))
                    Outline(canvas,{r.x+.5f,r.y+.5f,r.width-1,r.height-1},8.5f,lifted?.95f:.8f,lifted?1.6f:1.2f);
                if(active&&view.titleEditing)continue;
                Name(canvas,view.tabs[i],{r.x+10,r.y+6,r.width-20,18},13,WidgetTextAlign::Center);
            }
            if(view.tabInsert){
                // Insertion bar between tabs where the lifted tab will land.
                const auto slot=std::min(*view.tabInsert,layout.tabs.size());
                const auto& edge=slot<layout.tabs.size()?layout.tabs[slot]:layout.tabs.back();
                const float x=slot<layout.tabs.size()?edge.x-2.f:edge.x+edge.width+1.f;
                canvas.Rounded({x-1.5f,edge.y+3,3,edge.height-6},1.5f,0x8bc8ff,1);
            }
            canvas.PopClip();
        }
    }else{
        auto title=layout.title;title.x+=4;title.y+=5;title.width-=8;title.height=20;
        if(!view.titleEditing){
            if(width<200&&!layout.collapsed)Name(canvas,OrganizerWide(collection.title),title,14,WidgetTextAlign::Left);
            else canvas.Text(OrganizerWide(collection.title),title,layout.collapsed?16.f:14.f,
                0xffffff,1,WidgetTextWeight::Semibold,layout.collapsed?WidgetTextAlign::Center:WidgetTextAlign::Left);
        }
    }
    if(!layout.collapsed&&width>=200){
        constexpr float scale=16.f/24;
        for(unsigned i=0;i<4;++i)Outline(canvas,{24+(3+static_cast<float>(i%2)*11)*scale,
            28+(3+static_cast<float>(i/2)*11)*scale,7*scale,7*scale},2*scale,.8f,1.6f*scale);
    }
    SearchGlyph(canvas,layout.search);
    if(view.searchOpen&&!layout.collapsed){
        if(!view.searchEditing){
            if(view.query.empty())canvas.Text(L"搜索名称或关键词",layout.searchInput,12,0xffffff,.6f,false,WidgetTextAlign::Center);
            else canvas.Text(OrganizerWide(view.query),layout.searchInput,12,0xffffff,1,false,WidgetTextAlign::Center);
        }
    }
    if(layout.cells.empty()&&!layout.collapsed){
        if(view.filtered)canvas.Text(L"没有找到匹配的项目",{29,layout.viewport.y+30,width-58,16},12,0xffffff,.7f,false,WidgetTextAlign::Center);
        else{
            const float center=(height+20)/2;
            Outline(canvas,{width/2-16,center-43.5f,32,32},10,.5f,1.3f);
            canvas.Line(width/2-7,center-27.5f,width/2+7,center-27.5f,0xffffff,.5f,1.3f);
            canvas.Line(width/2,center-34.5f,width/2,center-20.5f,0xffffff,.5f,1.3f);
            canvas.Text(L"把图标放进来",{24,center+8.5f,width-48,19},14,0xffffff,.83f,WidgetTextWeight::Medium,WidgetTextAlign::Center);
            canvas.Text(L"拖入快捷方式、文件或文件夹",{24,center+36.5f,width-48,15},11,0xffffff,.5f,false,WidgetTextAlign::Center);
        }
    }
    if(!canvas.PushClip(layout.viewport))return;
    const bool list=collection.layout==organizer::Layout::List;
    const float iconScale=static_cast<float>(std::clamp(collection.iconScale,50,120))/100;
    if(motion){
        if(motion->external&&motion->feedback>0){
            canvas.Rounded(layout.viewport,8,0x8bc8ff,.10f*motion->feedback);
            const auto v=layout.viewport;
            Outline(canvas,{v.x+1,v.y+1,v.width-2,v.height-2},7,.8f*motion->feedback,1.5f);
        }
        for(const auto& slot:motion->slots){
            if(!Intersects(slot,layout.viewport))continue;
            canvas.Rounded(slot,8,0x8bc8ff,.075f*motion->feedback);
            Outline(canvas,{slot.x+2,slot.y+2,slot.width-4,slot.height-4},7,.45f*motion->feedback,1.2f);
        }
    }
    const auto drawCell=[&](std::size_t index,WidgetRect r,float scale,float lift,bool floating,bool showName=true){
        if(index>=collection.items.size())return;
        // Shell extraction belongs to OrganizerIconCache, never this paint path.
        const auto icon=IconBounds(r,layout.collapsed,list,scale,lift,motion&&!floating?&layout.viewport:nullptr,iconScale);
        if(!Intersects(r,layout.viewport)&&(!motion||!Intersects(icon,layout.viewport)))return;
        const bool selected=std::find(view.selection.begin(),view.selection.end(),index)!=view.selection.end();
        const bool hot=hover&&*hover==index,chosen=selected||(view.selected&&*view.selected==index);
        const auto& item=collection.items[index];
        // As on the Windows desktop: a pale tile under the pointer, a stronger
        // blue one with an edge when selected. A dragged icon carries none.
        // In the grid the tile is a fixed square around the icon and its name,
        // tall enough for the icon at its largest hover (scale 1.28, lifted 5),
        // so the icon never pokes out of it and the tile does not move.
        if((hot||chosen)&&!floating){
            WidgetRect tile{r.x+1,r.y+1,r.width-2,r.height-2};
            if(!list&&!layout.collapsed){
                const float top=r.y+2.34f*iconScale-8,bottom=r.y+52.5f*iconScale+27;
                const float side=bottom-top,width=std::min(side,r.width+8);
                tile={r.x+(r.width-width)/2,top,width,side};
                // A fully visible first row keeps its whole outline: the icon is
                // held inside the viewport there anyway (IconBounds).
                const auto v=layout.viewport;
                if(r.y>=v.y&&tile.y<v.y+1){tile.height-=v.y+1-tile.y;tile.y=v.y+1;}
            }
            const auto rgb=chosen?0x99d1ffu:0xcce8ffu;
            canvas.Rounded(tile,5,rgb,chosen?(hot?.4f:.32f):.18f);
            Outline(canvas,tile,5,chosen?.62f:.35f,1,rgb);
        }
        if(!canvas.ImageBitmap(image?image(item):nullptr,icon))Fallback(canvas,icon,item.kind);
        if(layout.collapsed||!showName)return;
        if(list)
            Name(canvas,OrganizerWide(item.name),{r.x+21+24*iconScale,r.y+(r.height-22)/2,std::max(0.f,r.width-52-24*iconScale),22},14,WidgetTextAlign::Left);
        else Name(canvas,OrganizerWide(item.name),{r.x+5,r.y+52.5f*iconScale+6,r.width-10,16},11,WidgetTextAlign::Center);
    };
    if(motion){
        const auto primary=std::find_if(motion->cells.rbegin(),motion->cells.rend(),[](const auto& cell){return cell.floating;});
        // 浮动项最后绘制；移动名称但仅对图标应用缩放/抬升。
        for(const bool floating:{false,true}){
            if(floating&&motion->marquee){
                const auto r=*motion->marquee;
                canvas.Rounded(r,2,0x8bc8ff,.13f);
                Outline(canvas,r,2,.8f,1);
            }
            for(const auto& cell:motion->cells){
                if(cell.floating!=floating)continue;
                const auto r=cell.bounds;
                const bool companion=floating&&primary!=motion->cells.rend()&&cell.index!=primary->index;
                drawCell(cell.index,r,cell.scale,cell.lift,floating,!companion);
            }
        }
        if(motion->external&&motion->feedback>0){
            const auto v=layout.viewport;
            CountBadge(canvas,{v.x+v.width-34,v.y+8,20,20},motion->count,v);
        }else if(motion->count>1){
            if(primary!=motion->cells.rend())CountBadge(canvas,
                IconBounds(primary->bounds,layout.collapsed,list,primary->scale,primary->lift,nullptr,iconScale),motion->count,layout.viewport);
        }
    }else for(const auto& cell:layout.cells){
        drawCell(cell.index,cell.bounds,1,0,false);
    }
    canvas.PopClip();
}
OrganizerHit HitOrganizer(float width,float height,const organizer::Collection& collection,float x,float y,const OrganizerViewState& view){
    const auto layout=LayoutOrganizer(width,height,collection,view);
    if(Contains(layout.search,x,y))return {OrganizerHit::Kind::Search,0};
    if(!layout.tabs.empty()){
        if(Contains(layout.tabStrip,x,y)){
            for(std::size_t i=0;i<layout.tabs.size();++i)
                if(Contains(layout.tabs[i],x,y))return {OrganizerHit::Kind::Tab,i};
            // Empty strip space still drags the window like the old title.
            return {OrganizerHit::Kind::Title,0};
        }
    }else if(Contains(layout.title,x,y))return {OrganizerHit::Kind::Title,0};
    if(!Contains(layout.viewport,x,y))return {};
    for(const auto& cell:layout.cells){
        if(!Intersects(cell.bounds,layout.viewport))continue;
        if(Contains(cell.bounds,x,y))return {OrganizerHit::Kind::Item,cell.index};
    }
    return {};
}
}
