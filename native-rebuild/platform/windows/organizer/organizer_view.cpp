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
void SearchGlyph(WidgetCanvas& canvas,WidgetRect r,float opacity){
    const float x=r.x+(r.width-16)/2,y=r.y+(r.height-16)/2;
    constexpr float scale=16.f/24,stroke=1.7f*scale;
    Outline(canvas,{x+4*scale,y+4*scale,13*scale,13*scale},6.5f*scale,.88f*opacity,stroke);
    canvas.Line(x+16*scale,y+16*scale,x+21*scale,y+21*scale,0xffffff,.88f*opacity,stroke);
    for(const float end:{16.f,21.f})
        canvas.Rounded({x+end*scale-stroke/2,y+end*scale-stroke/2,stroke,stroke},stroke/2,0xffffff,.88f*opacity);
}
void Name(WidgetCanvas& canvas,const std::wstring& text,WidgetRect r,float size,WidgetTextAlign align,
          bool centerVertically,WidgetTextWeight weight=WidgetTextWeight::Normal){
    if(r.width<=0||r.height<=0)return;
    if(centerVertically&&canvas.MeasureText(text,size,weight)<=r.width){
        canvas.Text(text,r,size,0xffffff,1,weight,align);
        return;
    }
    const float textHeight=canvas.MeasureWrappedText(text,r.width,size,weight,align);
    if(centerVertically)r.y+=std::max(0.f,(r.height-textHeight)/2);
    canvas.WrappedText(text,r,size,0xffffff,1,weight,align,textHeight>r.height);
}
constexpr float HeaderNameHeight=38.f;
constexpr float GridNameHeight=46.f;
float GridNameTop(float scale){return 52.5f*scale+5;}
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
// Estimate the single-line width here so hit rectangles stay stable without a
// canvas. The tab can use two lines if it reaches the strip width.
float OrganizerTabWidth(const std::wstring& name){
    float text=0;
    for(const auto c:name){
        if(c>=0xdc00&&c<=0xdfff)continue;
        text+=c>=0x2e80?13.f:(c==L' '?3.6f:(c>=L'A'&&c<=L'Z')?8.4f:7.f);
    }
    return std::max(56.f,text+24);
}
OrganizerLayout LayoutOrganizer(float width,float height,const organizer::Collection& collection,const OrganizerViewState& view){
    OrganizerLayout result;
    result.collapsed=collection.mode==organizer::Mode::Collapsed&&!view.temporaryExpanded&&!view.folding;
    if(result.collapsed){
        const float top=std::max(0.f,(height-HeaderNameHeight)/2);
        result.header={0,0,width,height};
        result.title={40,top,std::max(0.f,width-52),HeaderNameHeight};
        if(view.tabs.size()>1){
            result.tabStrip=result.title;
            float x=0;
            for(const auto& name:view.tabs){
                const float w=std::min(OrganizerTabWidth(name),result.tabStrip.width);
                result.tabs.push_back({result.tabStrip.x+x,top,w,HeaderNameHeight});x+=w+4;
            }
            result.maxTabScroll=std::max(0.f,x-4-result.tabStrip.width);
            const float scroll=std::clamp(view.tabScroll,0.f,result.maxTabScroll);
            for(auto& tab:result.tabs)tab.x-=scroll;
            result.title={};
            if(view.activeTab<result.tabs.size())result.title=result.tabs[view.activeTab];
        }
        return result;
    }
    const auto indices=Visible(collection,view);
    result.title={49,17,std::max(0.f,width-104),HeaderNameHeight};
    result.search={width-49,22,28,28};
    if(width<200){result.title={12,17,std::max(0.f,width-60),HeaderNameHeight};result.search={width-38,22,26,28};}
    result.title.x=40+(result.title.x-40)*view.unfold;
    result.title.y=3+14*view.unfold;
    result.header={0,0,width,58};
    if(view.tabs.size()>1){
        // 标签替代标题：位于网格图标与搜索之间，超出宽度时横向滚动。
        result.tabStrip={46,17,std::max(0.f,width-46-55),HeaderNameHeight};
        if(width<200)result.tabStrip={12,17,std::max(0.f,width-60),HeaderNameHeight};
        result.tabStrip.x=40+(result.tabStrip.x-40)*view.unfold;
        result.tabStrip.y=result.title.y;
        float x=0;
        for(const auto& name:view.tabs){
            const float w=std::min(OrganizerTabWidth(name),result.tabStrip.width);
            result.tabs.push_back({result.tabStrip.x+x,result.tabStrip.y,w,HeaderNameHeight});x+=w+4;
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
    const float cellHeight=list?std::max(42.f,42.f*iconScale):GridNameTop(iconScale)+GridNameHeight+3;
    const float gap=list?4.f:8.f;
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
    if(layout.collapsed||view.unfold<=0.f)return {};
    auto button=layout.search;
    const float inset=(1-view.unfold)*button.height/2;
    button.x+=inset;button.y+=inset;button.width-=2*inset;button.height-=2*inset;
    std::vector<LumeMaterialPanel> panels{{button.x,button.y,button.width,button.height,-1}};
    if(view.searchOpen){
        const auto input=layout.searchInput;
        const float height=input.height+4;
        panels.push_back({input.x-6,input.y-2,input.width+12,height,height/2});
    }
    return panels;
}
void DrawOrganizer(WidgetCanvas& canvas,float width,float height,const organizer::Collection& collection,
    std::optional<std::size_t> hover,const OrganizerImage& image,const OrganizerViewState& view,const OrganizerMotionFrame* motion){
    const auto layout=LayoutOrganizer(width,height,collection,view);
    if(view.mergeTarget||(layout.collapsed&&view.dropTarget)){
        // 另一个收纳拖到这里松手即合并为标签。
        const float inset=layout.collapsed?2.f:8.f,radius=layout.collapsed?8.f:18.f;
        canvas.Rounded({inset,inset,width-2*inset,height-2*inset},radius,0x8bc8ff,.10f);
        Outline(canvas,{inset+.5f,inset+.5f,width-2*inset-1,height-2*inset-1},radius-.5f,.85f,1.6f);
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
                Name(canvas,view.tabs[i],{r.x+10,r.y,r.width-20,r.height},13,WidgetTextAlign::Center,true);
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
        auto title=layout.title;title.x+=4;title.width-=8;
        if(!view.titleEditing){
            const auto weight=layout.collapsed||width<200?WidgetTextWeight::Normal:WidgetTextWeight::Semibold;
            Name(canvas,OrganizerWide(collection.title),title,14,WidgetTextAlign::Left,true,weight);
        }
    }
    if(layout.collapsed||width>=200){
        constexpr float scale=16.f/24;
        const float x=layout.collapsed?12.f:12.f+12.f*view.unfold,y=layout.collapsed?(height-16)/2:14.f+14.f*view.unfold;
        for(unsigned i=0;i<4;++i){
            const WidgetRect cell{x+(3+static_cast<float>(i%2)*11)*scale,
                y+(3+static_cast<float>(i/2)*11)*scale,7*scale,7*scale};
            Outline(canvas,cell,2*scale,.8f,1.6f*scale);
        }
    }
    // A folded organizer is only its title/tab bar. Ignore any motion frame
    // retained from the expanded view and never ask for item icons here.
    if(layout.collapsed)return;
    SearchGlyph(canvas,layout.search,view.unfold);
    if(view.searchOpen){
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
        // Leave room for the lifted icon and all lines of its label.
        if((hot||chosen)&&!floating){
            WidgetRect tile{r.x+1,r.y+1,r.width-2,r.height-2};
            if(!list&&!layout.collapsed){
                const float top=r.y+2.34f*iconScale-8;
                tile={r.x-4,top,r.width+8,r.y+r.height-1-top};
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
            Name(canvas,OrganizerWide(item.name),{r.x+21+24*iconScale,r.y+1,r.width-38-24*iconScale,r.height-2},
                14,WidgetTextAlign::Left,true);
        else Name(canvas,OrganizerWide(item.name),{r.x+3,r.y+GridNameTop(iconScale),r.width-6,
                std::min(GridNameHeight,r.height-GridNameTop(iconScale))},11,WidgetTextAlign::Center,false);
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
    if(layout.collapsed&&Contains(layout.header,x,y))return {OrganizerHit::Kind::Header,0};
    if(!Contains(layout.viewport,x,y))return {};
    for(const auto& cell:layout.cells){
        if(!Intersects(cell.bounds,layout.viewport))continue;
        if(Contains(cell.bounds,x,y))return {OrganizerHit::Kind::Item,cell.index};
    }
    return {};
}
}
