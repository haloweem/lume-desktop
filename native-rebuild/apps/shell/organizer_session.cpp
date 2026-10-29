#include "apps/shell/organizer_session.h"
#include "platform/windows/widget_ui/ui_font.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/organizer/organizer_view.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/shell_drag.h"
#include "platform/windows/organizer/organizer_icon_client.h"
#include "platform/windows/organizer/organizer_drop_target.h"
#include "platform/windows/organizer/organizer_motion.h"
#include "platform/windows/organizer/desktop_organizer_source.h"
#include "core/organizer/organizer_interaction.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/widget_ui/widget_placement.h"
#include <shellapi.h>
#include <objbase.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include "vendor/nlohmann/json.hpp"

namespace lume::shell {
using namespace platform::windows;
namespace {
// The feature supplies the stricter expanded/collapsed tracking sizes through
// WM_GETMINMAXINFO; this envelope also permits mode changes on the same HWND.
constexpr int ExpandedMinWidth=120;
constexpr int ExpandedMinHeight=216;
constexpr int ExpandedMaxWidth=1200;
constexpr int ExpandedMaxHeight=720;
constexpr widget::Limits Limits{420,392,ExpandedMinWidth,ExpandedMinHeight,ExpandedMaxWidth,ExpandedMaxHeight,true};
constexpr UINT SearchEditor=43101,TitleEditor=43102,CheckFocus=43103;
constexpr UINT PresentEditorMessage=WM_APP+0x6b1;
constexpr UINT_PTR EditorCaretTimer=43104;
constexpr UINT_PTR MotionTimer=43105;
// Long-press a tab to lift it out of its group; release on the strip reorders
// it, outside the window detaches it, over another collection's header moves
// it into that group. While the press is held, small hand movement (TabPressSlop
// DIPs) does not start moving the whole window.
constexpr UINT_PTR TabPressTimer=43106;
constexpr UINT TabPressMilliseconds=350;
constexpr int TabPressSlop=10;
constexpr wchar_t TabGhostClass[]=L"LumeOrganizerTabGhost";
// Icons dragged out over the desktop follow the pointer here, as on the Dock.
constexpr wchar_t RemoveGhostClass[]=L"LumeOrganizerDragIcon";
// Posted to the surviving target so a merge never destroys the window whose
// message handler is still on the stack.
constexpr UINT MergeCommand=43107;
// 删除图标 in an icon's menu: takes the selection out of the collection.
constexpr UINT RemoveCommand=13;
struct EditorDib {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
    EditorDib(int width,int height){
        dc=CreateCompatibleDC(nullptr);if(!dc)return;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(bitmap){previous=SelectObject(dc,bitmap);pixels=static_cast<std::uint32_t*>(bits);}
    }
    ~EditorDib(){if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    EditorDib(const EditorDib&)=delete;
    EditorDib& operator=(const EditorDib&)=delete;
};
std::string PathKey(std::string_view text){return OrganizerPathKey(text);}
std::wstring Wide(std::string_view text){return OrganizerWide(text);}
bool ItemHit(OrganizerHit hit){return hit.kind==OrganizerHit::Kind::Item;}
bool SameHit(OrganizerHit a,OrganizerHit b){return a.kind==b.kind&&a.index==b.index;}
std::wstring WindowText(HWND hwnd){
    const auto length=GetWindowTextLengthW(hwnd);
    if(length<0||length>1024)return {};
    std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(hwnd,value.data(),length+1)));
    return value;
}
std::string CreateUuid(){
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};
    wchar_t text[40]{};if(!StringFromGUID2(guid,text,40))return {};
    const std::wstring value(text);
    return OrganizerUtf8(std::wstring_view(value).substr(1,value.size()-2));
}
// The organizer holds desktop icons: it stays above every other widget (and
// below the Dock), so a clicked clock or player never covers its icons.
WidgetWindowPorts Shelf(WidgetWindowPorts ports){ports.shelf=true;return ports;}
}
struct OrganizerSession::Impl {
    struct Entry {
        Entry(Impl& owner,std::string id):owner(owner),id(std::move(id)),window(owner.instance,L"Lume 图标收纳",Limits,Shelf(WidgetWindowPorts{
            [this](WidgetCanvas& canvas,float w,float h){
                if(const auto* c=Collection()){
                    const auto now=GetTickCount64();
                    const float dt=lastMotionTick?std::min(.05f,static_cast<float>(now-lastMotionTick)/1000):0.f;
                    lastMotionTick=now;
                    const auto& frame=motion.Update(*c,LayoutOrganizer(w,h,*c,view),pointer,
                        interaction.Dragged(),dragPrimary,dragOffset,dt,reducedMotion,externalDrop,externalCount);
                    auto paintFrame=frame;paintFrame.marquee=marquee;
                    DrawOrganizer(canvas,w,h,*c,hover,
                        [this](const auto& item){return icons.Find(Wide(item.path));},view,&paintFrame);
                    ScheduleMotion();
                }
            },
            [this](WidgetAction action,const auto& bounds){this->owner.Native(*this,action,bounds);},
            [this]{iconUpdatePosted=false;window.Paint();},
            [this]{if(!this->owner.stopped&&!this->owner.applying)this->owner.Publish(false);},
            [this](HWND hwnd,UINT message,WPARAM w,LPARAM l){return Event(hwnd,message,w,l);},
            [this](float w,float h){
                const auto* c=Collection();if(!c)return std::vector<LumeMaterialPanel>{};
                return OrganizerSearchPanels(LayoutOrganizer(w,h,*c,view),view);
            },
            [this](float x,float y){
                const auto* c=Collection();if(!c)return false;
                if(Collapsed())return true;
                return HitOrganizer(static_cast<float>(window.Bounds().width),static_cast<float>(window.Bounds().height),*c,x,y,view).kind!=OrganizerHit::Kind::None;
            },false,true,false,[this](float,float){return Collapsed()?36u:0u;}
        })){}
        ~Entry(){Stop();}
        // id names the host collection that owns this window; Collection() is
        // the visible tab whose items are shown and edited.
        organizer::Collection* Collection(){
            const auto* c=organizer::ActiveTab(owner.state,id);return c?organizer::Find(owner.state,c->id):nullptr;
        }
        const organizer::Collection* Collection()const{return organizer::ActiveTab(owner.state,id);}
        std::string ContentId()const{const auto* c=Collection();return c?c->id:id;}
        // Header region in screen pixels, used as the merge drop zone.
        bool HeaderContains(POINT screen)const{
            const auto hwnd=window.Handle();RECT r{};
            if(!hwnd||!IsWindowVisible(hwnd)||!GetWindowRect(hwnd,&r)||!PtInRect(&r,screen))return false;
            if(Collapsed())return true;
            const auto dpi=GetDpiForWindow(hwnd);
            return screen.y<r.top+MulDiv(58,static_cast<int>(dpi?dpi:96),96);
        }
        bool WindowContains(POINT screen)const{
            const auto hwnd=window.Handle();RECT r{};
            return hwnd&&IsWindowVisible(hwnd)&&GetWindowRect(hwnd,&r)&&PtInRect(&r,screen);
        }
        void SyncTabs(){
            const auto tabs=organizer::Tabs(owner.state,id);
            const auto* active=Collection();
            view.tabs.clear();view.activeTab=0;
            if(tabs.size()>1)for(std::size_t i=0;i<tabs.size();++i){
                view.tabs.push_back(Wide(tabs[i]->title));
                if(tabs[i]==active)view.activeTab=i;
            }
            if(view.liftedTab&&*view.liftedTab>=view.tabs.size())view.liftedTab.reset();
            if(view.dropTab&&*view.dropTab>=view.tabs.size())view.dropTab.reset();
            const auto layout=Layout();
            if(view.activeTab<layout.tabs.size()){
                // Keep the visible tab inside the strip after a switch or rename.
                const auto r=layout.tabs[view.activeTab],strip=layout.tabStrip;
                if(r.x<strip.x)view.tabScroll-=strip.x-r.x;
                else if(r.x+r.width>strip.x+strip.width)view.tabScroll+=r.x+r.width-strip.x-strip.width;
            }
            view.tabScroll=std::clamp(view.tabScroll,0.f,Layout().maxTabScroll);
        }
        const organizer::Collection* Host()const{return organizer::Find(owner.state,id);}
        bool Locked()const{const auto* h=Host();return h&&h->locked;}
        void Activate(std::size_t index){
            const auto tab=TabId(index);if(tab.empty())return;
            if(tab==ContentId()){window.Paint();return;}
            FinishTitle(true);
            const bool ok=owner.Apply([&](auto& state){organizer::ActivateTab(state,id,tab);});
            owner.Publish(!ok);
        }
        void CycleTab(bool back){
            if(view.tabs.size()<2)return;
            const auto count=view.tabs.size();
            Activate(back?(view.activeTab+count-1)%count:(view.activeTab+1)%count);
        }
        // Starts the system move loop; on release over another collection's
        // header, the whole group is merged into that collection.
        void MoveWindow(HWND hwnd,LPARAM l){
            KillTimer(hwnd,TabPressTimer);view.liftedTab.reset();HideTabGhost();
            pressed.reset();ReleaseCapture();POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(hwnd,&point);
            windowMoving=true;
            SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));
            windowMoving=false;
            POINT cursor{};GetCursorPos(&cursor);
            owner.EndMergeHighlight();
            if(const auto* target=owner.MergeTargetAt(*this,cursor))owner.RequestMerge(id,target->id,false);
        }
        std::string TabId(std::size_t index)const{
            const auto tabs=organizer::Tabs(owner.state,id);
            return index<tabs.size()?tabs[index]->id:std::string{};
        }
        bool Collapsed()const{const auto* c=Collection();return c&&c->mode==organizer::Mode::Collapsed&&!view.temporaryExpanded;}
        OrganizerLayout Layout()const{
            const auto* c=Collection();const auto b=window.Bounds();
            return c?LayoutOrganizer(static_cast<float>(b.width),static_cast<float>(b.height),*c,view):OrganizerLayout{};
        }
        std::vector<std::size_t> VisibleIndices()const{
            const auto* c=Collection();if(!c)return {};
            auto visible=organizer::Search(*c,view.query,OrganizerSearchKey);
            if(Collapsed()&&visible.size()>4)visible.resize(4);
            return visible;
        }
        std::vector<std::string> ItemIds(bool visibleOnly=false)const{
            const auto* c=Collection();if(!c)return {};
            std::vector<std::string> ids;
            if(visibleOnly){for(const auto index:VisibleIndices())ids.push_back(c->items[index].id);}
            else for(const auto& item:c->items)ids.push_back(item.id);
            return ids;
        }
        void SyncSelection(){
            view.selection.clear();const auto* c=Collection();if(!c)return;
            for(std::size_t i=0;i<c->items.size();++i)
                if(interaction.IsSelected(c->items[i].id))view.selection.push_back(i);
        }
        void SelectFocus(std::size_t index,bool control=false,bool shift=false,bool preserve=false){
            const auto* c=Collection();if(!c||index>=c->items.size())return;
            interaction.Select(ItemIds(true),c->items[index].id,control,shift,preserve);
            view.selected=index;
            if(control&&!interaction.IsSelected(c->items[index].id)){
                view.selected.reset();
                if(!interaction.Selected().empty())for(std::size_t i=0;i<c->items.size();++i)
                    if(c->items[i].id==interaction.Selected().back())view.selected=i;
            }
            SyncSelection();
        }
        OrganizerPoint ClientPoint(HWND hwnd,LPARAM l)const{
            const auto dpi=GetDpiForWindow(hwnd);const float scale=96.f/(dpi?dpi:96);
            return {static_cast<float>(GET_X_LPARAM(l))*scale,static_cast<float>(GET_Y_LPARAM(l))*scale};
        }
        bool InViewport(OrganizerPoint p)const{
            const auto r=Layout().viewport;
            return p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;
        }
        void ScheduleMotion(){
            const auto hwnd=window.Handle();if(!hwnd)return;
            bool scrolling=false;
            if(pointer&&!Collapsed()&&(interaction.Dragging()||externalDrop||marquee)){
                const auto layout=Layout();const auto r=layout.viewport;
                scrolling=pointer->x>=r.x&&pointer->x<r.x+r.width&&pointer->y>=r.y-12&&pointer->y<=r.y+r.height+12&&
                    ((pointer->y<r.y+30&&view.scroll>0)||(pointer->y>r.y+r.height-30&&view.scroll<layout.maxScroll));
            }
            const bool needed=motion.Active()||scrolling;
            if(needed&&!motionTimer)motionTimer=SetTimer(hwnd,MotionTimer,16,nullptr)!=0;
            else if(!needed&&motionTimer){KillTimer(hwnd,MotionTimer);motionTimer=false;lastScrollTick=0;}
        }
        void RefreshMotionSettings(){
            BOOL enabled=TRUE;
            if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))reducedMotion=!enabled;
        }
        void PreviewDrag(OrganizerPoint point){
            if(!interaction.Dragging()||!InViewport(point))return;
            auto natural=view;natural.order.clear();
            const auto b=window.Bounds();
            const auto layout=LayoutOrganizer(static_cast<float>(b.width),static_cast<float>(b.height),*Collection(),natural);
            const auto& dragged=interaction.Dragged();
            for(const auto& cell:layout.cells){
                const auto r=cell.bounds;
                if(cell.index<Collection()->items.size()&&point.x>=r.x&&point.x<r.x+r.width&&point.y>=r.y&&point.y<r.y+r.height&&
                    std::find(dragged.begin(),dragged.end(),Collection()->items[cell.index].id)!=dragged.end())return;
            }
            std::vector<OrganizerCell> targets;targets.reserve(layout.cells.size());
            for(std::size_t i=0;i<layout.cells.size();++i){
                const auto& cell=layout.cells[i];
                if(cell.index>=Collection()->items.size()||std::find(dragged.begin(),dragged.end(),Collection()->items[cell.index].id)!=dragged.end())continue;
                targets.push_back(cell);
            }
            std::size_t slot=targets.size();float closest=1e30f;
            for(std::size_t i=0;i<targets.size();++i){
                const auto r=targets[i].bounds;
                const float dx=point.x-r.x-r.width/2,dy=point.y-r.y-r.height/2;
                const float distance=dx*dx+dy*dy;
                if(distance<closest){closest=distance;slot=i;}
            }
            if(!targets.empty()){
                const auto last=targets.back().bounds;
                if(point.y>=last.y+last.height||(point.y>=last.y&&point.x>=last.x+last.width))slot=targets.size();
            }
            interaction.MoveBefore(slot<targets.size()?
                std::optional<std::string>(Collection()->items[targets[slot].index].id):std::nullopt);
            view.order.clear();const auto* c=Collection();
            for(const auto& item:interaction.Preview()){
                const auto found=std::find_if(c->items.begin(),c->items.end(),[&](const auto& value){return value.id==item;});
                if(found!=c->items.end())view.order.push_back(static_cast<std::size_t>(found-c->items.begin()));
            }
        }
        void CancelDrag(){
            HideRemoveGhost();
            interaction.EndDrag();view.order.clear();dragPrimary.clear();dragOriginal.clear();marquee.reset();marqueeBase.clear();
            pressed.reset();dragging=false;lastClick=0;
            if(GetCapture()==window.Handle())ReleaseCapture();
            window.Paint();
        }
        void CommitDrag(bool inside){
            HideRemoveGhost();
            const auto preview=interaction.Preview();
            const auto* c=Collection();
            const auto focus=c&&view.selected&&*view.selected<c->items.size()?c->items[*view.selected].id:std::string{};
            interaction.EndDrag();view.order.clear();dragPrimary.clear();dragOriginal.clear();pressed.reset();dragging=false;
            if(GetCapture()==window.Handle())ReleaseCapture();
            bool ok=true;
            const auto content=ContentId();
            if(inside&&!preview.empty()&&preview!=ItemIds())
                ok=owner.Apply([&](auto& state){organizer::Reorder(*organizer::Find(state,content),preview);});
            c=Collection();
            if(c&&!focus.empty())for(std::size_t i=0;i<c->items.size();++i)if(c->items[i].id==focus)view.selected=i;
            SyncSelection();window.Paint();owner.Publish(!ok);
        }
        void DropFeedback(const OrganizerDropFeedback& feedback){
            if(stopping||owner.stopped)return;
            externalDrop=feedback.phase!=OrganizerDropFeedback::Phase::Leave&&feedback.supported;
            externalCount=externalDrop?feedback.count:0;
            const auto dpi=GetDpiForWindow(window.Handle());const float scale=96.f/(dpi?dpi:96);
            if(externalDrop)pointer=OrganizerPoint{feedback.point.x*scale,feedback.point.y*scale};
            else pointer.reset();
            window.Paint();
        }
        void MarqueeSelection(OrganizerPoint point){
            if(!marquee)return;
            auto start=ClientPoint(window.Handle(),MAKELPARAM(pressPoint.x,pressPoint.y));
            start.y+=marqueeStartScroll-view.scroll;
            const auto viewport=Layout().viewport;
            point.x=std::clamp(point.x,viewport.x,viewport.x+viewport.width);
            point.y=std::clamp(point.y,viewport.y,viewport.y+viewport.height);
            marquee=WidgetRect{std::min(start.x,point.x),std::min(start.y,point.y),std::abs(start.x-point.x),std::abs(start.y-point.y)};
            std::vector<std::string> selected=marqueeBase;
            for(const auto& cell:Layout().cells){
                const auto r=cell.bounds,m=*marquee;
                if(m.width>3&&m.height>3&&r.x<m.x+m.width&&r.x+r.width>m.x&&r.y<m.y+m.height&&r.y+r.height>m.y)
                    selected.push_back(Collection()->items[cell.index].id);
            }
            interaction.SelectAll(selected);SyncSelection();
            if(!view.selection.empty())view.selected=view.selection.back();else view.selected.reset();
        }
        void RevealSelection(){
            if(!view.selected||Collapsed())return;
            const auto layout=Layout();
            const auto selected=std::find_if(layout.cells.begin(),layout.cells.end(),
                [&](const auto& cell){return cell.index==*view.selected;});
            if(selected==layout.cells.end())return;
            if(selected->bounds.y<layout.viewport.y)view.scroll-=layout.viewport.y-selected->bounds.y;
            else if(selected->bounds.y+selected->bounds.height>layout.viewport.y+layout.viewport.height)
                view.scroll+=selected->bounds.y+selected->bounds.height-layout.viewport.y-layout.viewport.height;
            view.scroll=std::clamp(view.scroll,0.f,layout.maxScroll);
        }
        void Filter(){
            const auto* c=Collection();if(!c)return;
            view.visible=organizer::Search(*c,view.query,OrganizerSearchKey);
            view.filtered=organizer::HasSearchTerms(view.query,OrganizerSearchKey);
            if(view.selected&&(*view.selected>=c->items.size()||
                (view.filtered&&std::find(view.visible.begin(),view.visible.end(),*view.selected)==view.visible.end())))view.selected.reset();
            interaction.Reconcile(ItemIds(true));SyncSelection();
            view.scroll=std::clamp(view.scroll,0.f,Layout().maxScroll);
        }
        bool Configure(){
            auto* c=Collection();if(!c)return false;
            if(c->id!=shownId){
                // A different tab is now visible: its items, scroll and
                // selection share nothing with the previous tab.
                if(interaction.Dragging()||marquee)CancelDrag();
                if(!shownId.empty()){FinishTitle(false);CloseSearch();}
                interaction.Clear();view.selected.reset();view.selection.clear();view.order.clear();
                view.scroll=0;hover.reset();shownId=c->id;
            }
            if(interaction.Dragging()&&ItemIds()!=dragOriginal)CancelDrag();
            const auto* host=organizer::Find(owner.state,id);if(!host)return false;
            widget::Preferences p=widget::Defaults(Limits);p.enabled=true;p.locked=host->locked;p.frosted=host->frosted;
            if(host->bounds){p.placed=true;p.x=host->bounds->x;p.y=host->bounds->y;p.width=host->bounds->width;p.height=host->bounds->height;}
            if(Collapsed()){p.width=180;p.height=216;}
            else if(view.temporaryExpanded){p.width=420;p.height=392;}
            else{p.width=std::clamp(p.width,ExpandedMinWidth,ExpandedMaxWidth);
                p.height=std::clamp(p.height,ExpandedMinHeight,ExpandedMaxHeight);}
            if(!window.Configure(p)||!window.Start())return false;
            SyncTabs();Filter();
            RefreshMotionSettings();
            if(dropWindow!=window.Handle()){
                // Retry registration on the next Configure if OLE refused it.
                if(dropTarget.Attach(window.Handle(),[this](const auto& feedback){DropFeedback(feedback);},
                    [this](const auto& paths,POINT){const bool ok=owner.AddPaths(ContentId(),paths);owner.Publish(!ok);return ok;}))
                    dropWindow=window.Handle();
            }
            PositionEditor();
            if(!RefreshIcons())return false;
            // Start() painted before the tab strip and visible indices were
            // initialized. Commit that completed state before reporting ready.
            return window.Paint();
        }
        bool RefreshIcons(unsigned dpi=0){
            const auto* c=Collection();if(!c||!window.Handle())return false;
            std::vector<std::wstring> paths;paths.reserve(c->items.size());
            // Visible rows first, then the rest of this tab. Shell extraction
            // stays off-thread even when the collection has many items.
            const auto layout=Layout();std::set<std::string> queued;
            for(const auto& cell:layout.cells){
                if(cell.index>=c->items.size()||cell.bounds.y+cell.bounds.height<=layout.viewport.y||
                    cell.bounds.y>=layout.viewport.y+layout.viewport.height)continue;
                const auto& targetPath=c->items[cell.index].path;
                if(queued.insert(targetPath).second)paths.push_back(Wide(targetPath));
            }
            for(const auto& item:c->items)if(queued.insert(item.path).second)paths.push_back(Wide(item.path));
            const auto receiver=window.Handle();
            return icons.SetPaths(paths,[this,receiver]{
                if(!iconUpdatePosted.exchange(true)&&
                    !PostMessageW(receiver,NativeWidgetWindow::UpdateMessage,0,0))iconUpdatePosted=false;
            },OrganizerIconPixelsForDpi(MulDiv(static_cast<int>(dpi?dpi:GetDpiForWindow(receiver)),c->iconScale,100)));
        }
        void Stop(){
            stopping=true;dropTarget.Detach();dropWindow=nullptr;HideTabGhost();HideRemoveGhost();
            if(window.Handle()){KillTimer(window.Handle(),MotionTimer);KillTimer(window.Handle(),TabPressTimer);}
            shownId.clear();
            motionTimer=false;motion.Reset();interaction.Clear();interaction.EndDrag();pointer.reset();marquee.reset();
            externalDrop=false;externalCount=0;lastMotionTick=0;dragPrimary.clear();
            DestroyEditor();icons.Stop();window.Stop();iconUpdatePosted=false;
            view={};hover.reset();pressed.reset();lastClick=0;stopping=false;
        }
        void DestroyEditor(){
            const auto old=editor;editor=nullptr;
            if(old&&IsWindow(old)){KillTimer(old,EditorCaretTimer);DestroyWindow(old);}
            if(font){DeleteObject(font);font=nullptr;}
            view.titleEditing=false;view.searchEditing=false;composing=false;
            editorDpi=0;editorCaretInterval=0;editorPaintPending=false;editorPainting=false;editorCaretVisible=true;
        }
        void RequestEditorPaint(bool resetCaret=false){
            if(!editor||editorPainting)return;
            if(resetCaret)editorCaretVisible=true;
            if(!editorPaintPending)editorPaintPending=PostMessageW(editor,PresentEditorMessage,0,0)!=FALSE;
        }
        bool PresentEditor(){
            if(!editor||editorPainting)return false;
            RECT rect{};GetClientRect(editor,&rect);const int width=rect.right,height=rect.bottom;
            if(width<=0||height<=0)return false;
            EditorDib black(width,height),white(width,height);if(!black.pixels||!white.pixels)return false;
            const bool focused=GetFocus()==editor&&!composing;
            const auto blink=GetCaretBlinkTime();
            const UINT interval=focused&&blink&&blink!=INFINITE?std::max(250u,blink):0;
            if(interval!=editorCaretInterval){
                KillTimer(editor,EditorCaretTimer);editorCaretInterval=interval;editorCaretVisible=true;
                if(interval)SetTimer(editor,EditorCaretTimer,interval,nullptr);
            }
            editorPainting=true;
            // As with the native todo inputs, retain the real EDIT HWND and
            // derive foreground alpha from its black/white WM_PRINTCLIENT passes.
            for(int pass=0;pass<2;++pass){
                auto& dib=pass?white:black;editorMatte=pass?RGB(255,255,255):RGB(0,0,0);
                FillRect(dib.dc,&rect,static_cast<HBRUSH>(GetStockObject(pass?WHITE_BRUSH:BLACK_BRUSH)));
                CallWindowProcW(editorProcedure,editor,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dib.dc),PRF_CLIENT|PRF_ERASEBKGND);
                if(editorKind==SearchEditor&&!GetWindowTextLengthW(editor)&&!composing){
                    RECT text{};SendMessageW(editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&text));
                    text.top=rect.top;text.bottom=rect.bottom;
                    const BYTE channel=pass?255:153;
                    direct_write::Style style;style.size=12.f*(editorDpi?editorDpi:96)/96.f;
                    direct_write::Draw(dib.dc,L"搜索名称或关键词",text,RGB(channel,channel,channel),style);
                }
                GUITHREADINFO caret{};caret.cbSize=sizeof(caret);
                if(focused&&editorCaretVisible&&GetGUIThreadInfo(GetCurrentThreadId(),&caret)&&caret.hwndCaret==editor)
                    FillRect(dib.dc,&caret.rcCaret,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
            editorPainting=false;GdiFlush();
            const float scale=static_cast<float>(editorDpi?editorDpi:96)/96;
            const float radius=editorKind==TitleEditor?std::min({5.f*scale,width*.5f,height*.5f}):0.f;
            const auto coverage=[&](float x,float y,float inset){
                const float r=std::max(0.f,radius-inset);
                const float dx=std::max({r-(x-inset),x-(width-inset-r),0.f});
                const float dy=std::max({r-(y-inset),y-(height-inset-r),0.f});
                return std::clamp(r+.5f-std::hypot(dx,dy),0.f,1.f);
            };
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                const auto index=static_cast<std::size_t>(y)*width+x;
                const auto b=black.pixels[index],w=white.pixels[index];int alpha=0;
                for(const int shift:{0,8,16})
                    alpha=std::max(alpha,255-static_cast<int>((w>>shift)&255)+static_cast<int>((b>>shift)&255));
                alpha=std::clamp(alpha,0,255);
                const float outer=radius?coverage(x+.5f,y+.5f,0):1.f;
                const float border=radius?(1-coverage(x+.5f,y+.5f,scale))*.25f:0.f;
                const int framedAlpha=static_cast<int>(std::lround(255*border+alpha*(1-border)));
                auto pixel=static_cast<std::uint32_t>(std::lround(framedAlpha*outer))<<24;
                for(const int shift:{0,8,16})
                    pixel|=static_cast<std::uint32_t>(std::lround(std::min(static_cast<int>((b>>shift)&255),alpha)*(1-border)*outer+255*border*outer))<<shift;
                // Empty search pixels must still accept mouse selection/focus.
                if(outer==1.f&&!(pixel>>24))pixel=0x01000000u;
                black.pixels[index]=pixel;
            }
            POINT origin{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            return UpdateLayeredWindow(editor,nullptr,nullptr,&size,black.dc,&origin,0,&blend,ULW_ALPHA)!=FALSE;
        }
        void PositionEditor(){
            if(!editor||!window.Handle())return;
            const auto layout=Layout();
            const auto rect=editorKind!=TitleEditor?layout.searchInput:layout.tabs.empty()?layout.title:layout.tabStrip;
            const auto dpi=GetDpiForWindow(window.Handle());const auto scale=static_cast<float>(dpi?dpi:96)/96.f;
            POINT origin{static_cast<LONG>(rect.x*scale),static_cast<LONG>(rect.y*scale)};
            ClientToScreen(window.Handle(),&origin);
            SetWindowPos(editor,nullptr,origin.x,origin.y,static_cast<int>(rect.width*scale),
                static_cast<int>(rect.height*scale),SWP_NOACTIVATE|SWP_NOZORDER);
            if(editorDpi!=(dpi?dpi:96)){
                editorDpi=dpi?dpi:96;
                const auto replacement=CreateFontW(-MulDiv(editorKind==TitleEditor?14:12,static_cast<int>(editorDpi),96),
                    0,0,0,editorKind==TitleEditor?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                    // Subpixel ClearType assumes an opaque known background;
                    // grayscale coverage stays neutral over the live glass.
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,ui_font::Family(L"整理"));
                if(replacement){
                    SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(replacement),FALSE);
                    if(font)DeleteObject(font);
                    font=replacement;
                }
                const int padding=editorKind==TitleEditor?MulDiv(3,static_cast<int>(editorDpi),96):0;
                SendMessageW(editor,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(padding,padding));
            }
            RequestEditorPaint();
        }
        bool BeginEditor(UINT kind){
            if(!window.Handle())return false;
            if(editor&&editorKind==kind){SetFocus(editor);return true;}
            FinishTitle(true);DestroyEditor();editorKind=kind;
            auto* c=Collection();if(!c)return false;
            const auto text=kind==TitleEditor?Wide(c->title):Wide(view.query);
            // An owned popup EDIT is composited separately from UpdateLayeredWindow.
            // It supplies standard selection, clipboard, accessibility and IME.
            editor=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"EDIT",text.c_str(),
                WS_POPUP|ES_AUTOHSCROLL|(kind==SearchEditor?ES_CENTER:0),0,0,1,1,window.Handle(),nullptr,owner.instance,nullptr);
            if(!editor){owner.error="editor";return false;}
            if(!direct_write_edit::Attach(editor)){DestroyWindow(editor);editor=nullptr;owner.error="editor";return false;}
            SetPropW(editor,L"LumeOrganizerEditor",reinterpret_cast<HANDLE>(this));
            editorProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(editor,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(EditProcedure)));
            // Titles allow 80 code points; an 80-unit edit never overflows.
            SendMessageW(editor,EM_SETLIMITTEXT,kind==TitleEditor?organizer::MaxTitleLength:200,0);
            view.titleEditing=kind==TitleEditor;view.searchEditing=kind==SearchEditor;PositionEditor();
            if(!PresentEditor()){DestroyEditor();owner.error="editor";return false;}
            ShowWindow(editor,SW_SHOW);desktop_level::KeepAbove(editor,window.Handle());SetFocus(editor);
            if(kind==TitleEditor)SendMessageW(editor,EM_SETSEL,0,-1);
            window.Paint();return true;
        }
        static LRESULT CALLBACK EditProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
            auto* e=reinterpret_cast<Entry*>(GetPropW(hwnd,L"LumeOrganizerEditor"));
            if(!e)return DefWindowProcW(hwnd,message,w,l);
            const auto original=e->editorProcedure;
            if(message==PresentEditorMessage){e->editorPaintPending=false;e->PresentEditor();return 0;}
            if(message==WM_TIMER&&w==EditorCaretTimer){e->editorCaretVisible=!e->editorCaretVisible;e->RequestEditorPaint();return 0;}
            if(message==WM_PAINT){PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);e->RequestEditorPaint();return 0;}
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_IME_STARTCOMPOSITION)e->composing=true;
            if(message==WM_IME_ENDCOMPOSITION)e->composing=false;
            if(message==WM_KEYDOWN&&!e->composing&&(w==VK_RETURN||w==VK_ESCAPE)){
                if(e->editorKind==TitleEditor)e->FinishTitle(w==VK_RETURN);
                else{e->CloseSearch();const auto* c=e->Collection();if(w==VK_RETURN&&c&&!c->items.empty())e->view.selected=0;}
                if(e->window.Handle()){SetFocus(e->window.Handle());e->window.Paint();}
                return 0;
            }
            if(message==WM_KILLFOCUS){
                if(e->editorKind==TitleEditor)e->FinishTitle(true);
                if(e->window.Handle())PostMessageW(e->window.Handle(),WM_COMMAND,CheckFocus,0);
                if(!IsWindow(hwnd))return 0;
            }
            const auto result=CallWindowProcW(original,hwnd,message,w,l);
            if(message==WM_NCDESTROY){KillTimer(hwnd,EditorCaretTimer);RemovePropW(hwnd,L"LumeOrganizerEditor");return result;}
            if(e->editor==hwnd&&e->editorKind==SearchEditor&&
                (message==WM_CHAR||message==WM_SETTEXT||message==WM_PASTE||message==WM_CUT||
                 message==WM_CLEAR||message==WM_UNDO||message==EM_REPLACESEL||message==WM_IME_COMPOSITION||message==WM_IME_ENDCOMPOSITION)){
                e->view.query=OrganizerUtf8(WindowText(hwnd));e->view.scroll=0;e->pressed.reset();e->Filter();e->window.Paint();
            }
            if(e->editor==hwnd){
                switch(message){
                case WM_SETTEXT:case WM_SETFONT:case WM_SIZE:case WM_SETFOCUS:case WM_KILLFOCUS:
                case WM_CHAR:case WM_KEYDOWN:case WM_KEYUP:case WM_LBUTTONDOWN:case WM_LBUTTONUP:
                case WM_HSCROLL:case WM_VSCROLL:case EM_SETSEL:case EM_REPLACESEL:
                case WM_CUT:case WM_PASTE:case WM_CLEAR:case WM_UNDO:
                case WM_IME_STARTCOMPOSITION:case WM_IME_COMPOSITION:case WM_IME_ENDCOMPOSITION:
                    e->RequestEditorPaint(true);break;
                case WM_MOUSEMOVE:if(w&MK_LBUTTON)e->RequestEditorPaint(true);break;
                default:break;
                }
            }
            return result;
        }
        void FinishTitle(bool commit){
            if(!editor||editorKind!=TitleEditor)return;
            const auto text=OrganizerUtf8(WindowText(editor));
            DestroyEditor();
            if(commit){
                const auto* c=Collection();
                const auto title=organizer::SanitizeTitle(text);
                const auto content=ContentId();
                if(c&&title&&*title!=c->title){
                    const bool ok=owner.Apply([&](auto& state){organizer::Rename(*organizer::Find(state,content),*title);});
                    owner.Publish(!ok);
                }else if(c&&!title){owner.error="invalid";owner.Publish(true);}
            }
            window.Paint();
        }
        void CloseSearch(){
            if(editorKind==SearchEditor)DestroyEditor();
            view.searchOpen=false;view.query.clear();view.filtered=false;view.visible.clear();view.scroll=0;
            pressed.reset();window.Paint();
        }
        void ToggleSearch(){
            if(view.searchOpen){CloseSearch();SetFocus(window.Handle());return;}
            FinishTitle(true);view.searchOpen=true;
            if(Collapsed()){view.temporaryExpanded=true;view.scroll=0;if(!Configure()){view.temporaryExpanded=false;owner.error="runtime";return;}}
            BeginEditor(SearchEditor);window.Paint();
        }
        void CheckLostFocus(){
            if(stopping||owner.applying||menu)return;
            const auto focus=GetFocus();
            if(focus==window.Handle()||focus==editor||(editor&&IsChild(editor,focus)))return;
            if(view.temporaryExpanded){
                CloseSearch();FinishTitle(true);view.temporaryExpanded=false;view.scroll=0;Configure();
            }else if(view.searchOpen)CloseSearch();  // clicked elsewhere, e.g. the desktop
        }
        // Lifted-tab feedback: a small card that follows the cursor and says
        // what releasing here will do. 0 reorder, 1 drag out, 2 detach, 3 merge.
        void ShowTabGhost(int hint){
            const auto hwnd=window.Handle();
            if(!hwnd||!view.liftedTab||*view.liftedTab>=view.tabs.size())return;
            if(!tabGhost){
                static const bool registered=[]{
                    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=DefWindowProcW;
                    wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=TabGhostClass;
                    return RegisterClassExW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
                }();
                if(!registered)return;
                tabGhost=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
                    TabGhostClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                if(!tabGhost)return;
                ghostState=-1;
            }
            const auto dpi=GetDpiForWindow(hwnd)?GetDpiForWindow(hwnd):96u;
            if(hint!=ghostState){
                static const wchar_t* hints[]{L"松开以调整顺序",L"拖出窗口以分离",L"松开以分离为新窗口",L"松开以合并到该收纳"};
                WidgetCanvas canvas;WidgetBitmap bitmap;
                constexpr float w=188,h=48;
                if(!canvas.Begin(w,h,dpi))return;
                const bool ready=hint!=1;
                canvas.Rounded({0,0,w,h},12,ready?0x2f7cf6:0x2a2f3a,.94f);
                canvas.Text(view.tabs[*view.liftedTab],{14,6,w-28,20},13,0xffffff,1,WidgetTextWeight::Semibold);
                canvas.Text(hints[std::clamp(hint,0,3)],{14,26,w-28,16},11,0xffffff,ready?.95f:.7f);
                if(!canvas.Finish(bitmap)||!ghostSurface.Upload(bitmap))return;
                ghostState=hint;
            }
            POINT cursor{};GetCursorPos(&cursor);
            const POINT at{cursor.x+MulDiv(16,static_cast<int>(dpi),96),cursor.y+MulDiv(18,static_cast<int>(dpi),96)};
            ghostSurface.Compose(tabGhost,at,{static_cast<LONG>(ghostSurface.Width()),static_cast<LONG>(ghostSurface.Height())});
            if(!IsWindowVisible(tabGhost))ShowWindow(tabGhost,SW_SHOWNOACTIVATE);
        }
        // As the Dock does: the dragged icon follows the pointer over the
        // desktop with "移除" above it; releasing takes it out of the organizer.
        void ShowRemoveGhost(POINT cursor){
            const auto hwnd=window.Handle();const auto* c=Collection();
            if(!hwnd||!c)return;
            const auto found=std::find_if(c->items.begin(),c->items.end(),[&](const auto& item){return item.id==dragPrimary;});
            if(found==c->items.end())return;
            if(!removeGhost){
                static const bool registered=[]{
                    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=DefWindowProcW;
                    wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=RemoveGhostClass;
                    return RegisterClassExW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
                }();
                if(!registered)return;
                removeGhost=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
                    RemoveGhostClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                if(!removeGhost)return;
                removeGhostDrawn=false;
            }
            const auto dpi=GetDpiForWindow(hwnd)?GetDpiForWindow(hwnd):96u;
            // The icon as the Dock shows it: a little larger than at rest.
            const float size=std::max(40.f,44.f*static_cast<float>(c->iconScale)/100*1.12f),labelHeight=26,labelGap=8;
            const float width=std::max(size,88.f),height=labelHeight+labelGap+size;
            if(!removeGhostDrawn){
                WidgetCanvas canvas;WidgetBitmap bitmap;
                if(!canvas.Begin(width,height,dpi))return;
                const WidgetRect r{(width-size)/2,labelHeight+labelGap,size,size};
                if(!canvas.ImageBitmap(icons.Find(Wide(found->path)),r,.8f))canvas.Rounded(r,size*.22f,0xffffff,.14f);
                const auto count=interaction.Dragged().size();
                const std::wstring label=count>1?L"移除 "+std::to_wstring(count)+L" 项":L"移除";
                const float labelWidth=canvas.MeasureText(label,12)+22;
                const WidgetRect pill{(width-labelWidth)/2,0,labelWidth,labelHeight};
                canvas.Rounded(pill,8,0x1c1c1e,.78f);
                canvas.Text(label,pill,12,0xffffff,.95f,false,WidgetTextAlign::Center);
                if(!canvas.Finish(bitmap)||!removeGhostSurface.Upload(bitmap))return;
                removeGhostDrawn=true;
            }
            // The icon stays under the pointer where it was grabbed (its centre).
            const POINT at{cursor.x-MulDiv(static_cast<int>(width/2),static_cast<int>(dpi),96),
                cursor.y-MulDiv(static_cast<int>(labelHeight+labelGap+size/2),static_cast<int>(dpi),96)};
            removeGhostSurface.Compose(removeGhost,at,{static_cast<LONG>(removeGhostSurface.Width()),static_cast<LONG>(removeGhostSurface.Height())});
            if(!IsWindowVisible(removeGhost))ShowWindow(removeGhost,SW_SHOWNOACTIVATE);
        }
        bool RemoveGhostShown()const{return removeGhost&&IsWindowVisible(removeGhost);}
        void HideRemoveGhost(){
            removeGhostDrawn=false;
            if(removeGhost){const auto old=removeGhost;removeGhost=nullptr;DestroyWindow(old);}
            removeGhostSurface.Release();
        }
        void HideTabGhost(){
            view.tabInsert.reset();ghostState=-1;
            if(tabGhost){const auto old=tabGhost;tabGhost=nullptr;DestroyWindow(old);}
            ghostSurface.Release();
        }
        // Updates insertion slot, merge highlight and ghost for a lifted tab.
        void TrackLiftedTab(HWND hwnd,LPARAM l){
            POINT cursor{};GetCursorPos(&cursor);
            owner.HighlightMerge(*this,cursor);
            const auto p=ClientPoint(hwnd,l);const auto layout=Layout();const auto s=layout.tabStrip;
            std::optional<std::size_t> slot;
            if(WindowContains(cursor)&&p.y>=s.y-12&&p.y<s.y+s.height+12&&p.x>=s.x-12&&p.x<s.x+s.width+12){
                std::size_t n=0;
                for(const auto& tab:layout.tabs)if(tab.x+tab.width/2<p.x)++n;
                slot=n;
            }
            view.tabInsert=slot;
            const int hint=owner.MergeTargetAt(*this,cursor)?3:slot?0:WindowContains(cursor)?1:2;
            ShowTabGhost(hint);
        }
        void Open(std::size_t index){
            const auto* c=Collection();if(!c||index>=c->items.size())return;
            const auto itemPath=Wide(c->items[index].path);
            if(GetFileAttributesW(itemPath.c_str())!=INVALID_FILE_ATTRIBUTES){
                // Start the program before any repaint (closing the search redraws).
                const bool opened=owner.open(window.Handle(),itemPath);
                if(view.searchOpen)CloseSearch();
                if(!opened){owner.error="open";owner.Publish(true);}
                return;
            }
            CloseSearch();
            // The shell host would fail silently; offer to drop the stale entry.
            const auto name=Wide(c->items[index].name);const auto itemId=c->items[index].id;
            const auto text=L"“"+name+L"”已不存在，可能已被删除或移动。\n是否从收纳中移除？";
            if(MessageBoxW(window.Handle(),text.c_str(),L"桌面收纳",MB_YESNO|MB_ICONQUESTION)==IDYES){
                const auto* current=Collection();
                for(std::size_t i=0;current&&i<current->items.size();++i)
                    if(current->items[i].id==itemId){Remove(i);break;}
            }else{owner.error="target";owner.Publish(true);}
        }
        void Remove(std::size_t index){
            const auto* c=Collection();if(!c||index>=c->items.size())return;
            const auto itemId=c->items[index].id,content=ContentId();
            // Indices shift after removal; keep keyboard focus on the same item.
            const auto focus=view.selected&&*view.selected<c->items.size()&&*view.selected!=index?
                c->items[*view.selected].id:std::string{};
            pressed.reset();dragging=false;if(GetCapture()==window.Handle())ReleaseCapture();
            const bool ok=owner.Apply([&](auto& state){organizer::RemoveItem(state,content,itemId);});
            if(ok){
                view.selected.reset();c=Collection();
                for(std::size_t i=0;c&&!focus.empty()&&i<c->items.size();++i)if(c->items[i].id==focus)view.selected=i;
                SyncSelection();window.Paint();
            }
            owner.Publish(!ok);
        }
        void RemoveSelection(){RemoveIds(interaction.Selected());}
        void RemoveIds(const std::vector<std::string>& ids){
            if(ids.empty()||!Collection())return;
            pressed.reset();dragging=false;if(GetCapture()==window.Handle())ReleaseCapture();
            const auto content=ContentId();
            const bool ok=owner.Apply([&](auto& state){
                for(const auto& item:ids)organizer::RemoveItem(state,content,item);
            });
            if(ok){interaction.Clear();view.selected.reset();SyncSelection();}
            owner.Publish(!ok);
        }
        void Command(UINT command){
            if(owner.stopped||owner.applying)return;
            if(command==CheckFocus){CheckLostFocus();return;}
            if(command==MergeCommand){owner.FinishMerge();return;}
            if(interaction.Dragging()||marquee)CancelDrag();
            if(command==RemoveCommand){
                if(!interaction.Selected().empty())RemoveSelection();else if(view.selected)Remove(*view.selected);
                return;
            }
            if(command==3){owner.Create(*this);return;}
            if(command==11){if(owner.openSettings)owner.openSettings();return;}
            if(command==1){owner.Close(*this);return;}
            if(command==8||command==9){owner.Native(*this,command==8?WidgetAction::Liquid:WidgetAction::Frosted,window.Bounds());return;}
            if(command==10){owner.Native(*this,WidgetAction::Reset,window.Bounds());return;}
            if(command==12){owner.Native(*this,WidgetAction::Lock,window.Bounds());return;}
            if(command>=150&&command<=220){
                const bool ok=owner.Apply([&](auto& state){organizer::Find(state,id)->iconScale=static_cast<int>(command)-100;});
                owner.Publish(!ok);return;
            }
            if(command!=6&&command!=7)return;
            const bool ok=owner.Apply([&](auto& state){
                auto* c=organizer::Find(state,id);
                c->layout=command==6?organizer::Layout::List:organizer::Layout::Icons;
            });
            owner.Publish(!ok);
        }
        void Menu(LPARAM l){
            if(menu)return;
            if(pressed||interaction.Dragging()||marquee)CancelDrag();
            pointer.reset();hover.reset();window.Paint();
            POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(point.x==-1&&point.y==-1)GetCursorPos(&point);
            const auto* c=Collection();if(!c)return;
            // On an icon (or the Menu key with one focused) the menu acts on it:
            // a click outside the current selection selects just that icon.
            bool item=false;
            if(GET_X_LPARAM(l)==-1&&GET_Y_LPARAM(l)==-1)item=view.selected.has_value();
            else{
                POINT client=point;ScreenToClient(window.Handle(),&client);
                const auto p=ClientPoint(window.Handle(),MAKELPARAM(client.x,client.y));const auto b=window.Bounds();
                const auto hit=HitOrganizer(static_cast<float>(b.width),static_cast<float>(b.height),*c,p.x,p.y,view);
                if(hit.kind==OrganizerHit::Kind::Item){
                    item=true;
                    if(!interaction.IsSelected(c->items[hit.index].id))SelectFocus(hit.index);
                    window.Paint();
                }
            }
            Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
            if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return;
            menu=true;
            const auto command=LumeWidgetMenu::ShowOrganizer(window.Handle(),point,
                c->layout==organizer::Layout::List,c->frosted,c->locked,c->iconScale,item);
            menu=false;Gdiplus::GdiplusShutdown(token);
            Command(command);
        }
        std::optional<LRESULT> Event(HWND hwnd,UINT message,WPARAM w,LPARAM l){
            if(owner.stopped||stopping||!Collection())return {};
            const auto hitAt=[&]{
                const auto dpi=GetDpiForWindow(hwnd);const auto scale=96.f/(dpi?dpi:96);
                const auto b=window.Bounds();
                const float x=static_cast<float>(GET_X_LPARAM(l))*scale,y=static_cast<float>(GET_Y_LPARAM(l))*scale;
                auto hit=HitOrganizer(static_cast<float>(b.width),static_cast<float>(b.height),*Collection(),x,y,view);
                if(hit.kind==OrganizerHit::Kind::None&&(Collapsed()||y<Layout().header.height))hit={OrganizerHit::Kind::Header,0};
                return hit;
            };
            if(message==WM_SETTINGCHANGE){RefreshMotionSettings();window.Paint();return {};}
            if(message==WM_TIMER&&w==MotionTimer){
                const auto now=GetTickCount64();
                const float elapsed=lastScrollTick?std::min(.05f,static_cast<float>(now-lastScrollTick)/1000):.016f;
                lastScrollTick=now;
                if(pointer&&!Collapsed()&&(interaction.Dragging()||externalDrop||marquee)){
                    const auto layout=Layout();const auto r=layout.viewport;
                    if(pointer->x>=r.x&&pointer->x<r.x+r.width&&pointer->y>=r.y-12&&pointer->y<=r.y+r.height+12){
                        float speed=0;
                        if(pointer->y<r.y+30)speed=-420*std::clamp((r.y+30-pointer->y)/30,0.f,1.f);
                        else if(pointer->y>r.y+r.height-30)speed=420*std::clamp((pointer->y-r.y-r.height+30)/30,0.f,1.f);
                        const auto old=view.scroll;view.scroll=std::clamp(view.scroll+speed*elapsed,0.f,layout.maxScroll);
                        if(old!=view.scroll){if(interaction.Dragging())PreviewDrag(*pointer);if(marquee)MarqueeSelection(*pointer);}
                    }
                }
                window.Paint();return 0;
            }
            if(message==WM_CONTEXTMENU){Menu(l);return 0;}
            if(message==WM_COMMAND){
                const auto command=LOWORD(w);
                if(command==CheckFocus||command==MergeCommand||(command>=1&&command<=RemoveCommand)||(command>=150&&command<=220)){Command(command);return 0;}
            }
            if(message==WM_CTLCOLOREDIT&&editor&&reinterpret_cast<HWND>(l)==editor){
                const auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,RGB(255,255,255));
                // Original organizer inputs: transparent search, white 6% title.
                const BYTE channel=editorMatte?255:editorKind==TitleEditor?15:0;
                const auto color=RGB(channel,channel,channel);SetBkColor(dc,color);SetDCBrushColor(dc,color);
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            if(message==WM_GETMINMAXINFO){
                auto* limits=reinterpret_cast<MINMAXINFO*>(l);const auto dpi=static_cast<int>(GetDpiForWindow(hwnd));
                limits->ptMinTrackSize={MulDiv(Collapsed()?180:ExpandedMinWidth,dpi?dpi:96,96),
                    MulDiv(Collapsed()?216:ExpandedMinHeight,dpi?dpi:96,96)};
                limits->ptMaxTrackSize={MulDiv(Collapsed()?180:ExpandedMaxWidth,dpi?dpi:96,96),
                    MulDiv(Collapsed()?216:ExpandedMaxHeight,dpi?dpi:96,96)};
                return 0;
            }
            if(message==WM_TIMER&&w==TabPressTimer){
                KillTimer(hwnd,TabPressTimer);
                if(pressed&&pressed->kind==OrganizerHit::Kind::Tab&&!dragging&&view.tabs.size()>1){
                    view.liftedTab=pressed->index;dragging=true;SetCursor(LoadCursorW(nullptr,IDC_SIZEALL));
                    POINT cursor{};GetCursorPos(&cursor);ScreenToClient(hwnd,&cursor);
                    TrackLiftedTab(hwnd,MAKELPARAM(cursor.x,cursor.y));window.Paint();
                }
                return 0;
            }
            if(message==WM_MOVE&&windowMoving){POINT cursor{};GetCursorPos(&cursor);owner.HighlightMerge(*this,cursor);}
            if(message==WM_SIZE||message==WM_MOVE||message==WM_DPICHANGED){
                PositionEditor();
                if(message==WM_DPICHANGED)RefreshIcons(HIWORD(w));
                return {};
            }
            if(message==WM_KILLFOCUS){
                if(interaction.Dragging()||marquee)CancelDrag();
                pointer.reset();PostMessageW(hwnd,WM_COMMAND,CheckFocus,0);window.Paint();return {};
            }
            if(message==WM_MOUSEWHEEL){
                if(Collapsed())return 0;
                if(!view.tabs.empty()){
                    POINT screen{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&screen);
                    const auto p=ClientPoint(hwnd,MAKELPARAM(screen.x,screen.y));const auto layout=Layout();
                    const auto s=layout.tabStrip;
                    if(p.x>=s.x&&p.x<s.x+s.width&&p.y>=s.y&&p.y<s.y+s.height){
                        view.tabScroll=std::clamp(view.tabScroll-static_cast<float>(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA*60.f,0.f,layout.maxTabScroll);
                        window.Paint();return 0;
                    }
                }
                view.scroll=std::clamp(view.scroll-static_cast<float>(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA*90.f,0.f,Layout().maxScroll);
                RefreshIcons();
                if(pointer&&interaction.Dragging())PreviewDrag(*pointer);
                hover.reset();window.Paint();return 0;
            }
            if(message==WM_LBUTTONDBLCLK){
                const auto hit=hitAt();lastClick=0;pressed.reset();
                if(hit.kind==OrganizerHit::Kind::Item){
                    if(w&(MK_CONTROL|MK_SHIFT)){SelectFocus(hit.index,(w&MK_CONTROL)!=0,(w&MK_SHIFT)!=0);window.Paint();return 0;}
                    // Launch first; selection and repaint follow so they never delay the program.
                    titleFocused=false;Open(hit.index);SelectFocus(hit.index);window.Paint();return 0;
                }
                if(hit.kind==OrganizerHit::Kind::Title){BeginEditor(TitleEditor);return 0;}
                if(hit.kind==OrganizerHit::Kind::Tab){
                    KillTimer(hwnd,TabPressTimer);view.liftedTab.reset();
                    if(TabId(hit.index)!=ContentId())Activate(hit.index);
                    BeginEditor(TitleEditor);return 0;
                }
            }
            if(message==WM_LBUTTONDOWN){
                FinishTitle(true);
                const auto hit=hitAt();
                if(hit.kind==OrganizerHit::Kind::None&&view.searchOpen){
                    CloseSearch();
                    SetFocus(hwnd);
                    return 0;
                }
                if(hit.kind!=OrganizerHit::Kind::None){
                    pressed=hit;pressPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};dragging=false;
                    pointer=ClientPoint(hwnd,l);clickCollapse=false;
                    titleFocused=hit.kind==OrganizerHit::Kind::Title||hit.kind==OrganizerHit::Kind::Tab;
                    if(hit.kind==OrganizerHit::Kind::Tab){
                        GetCursorPos(&tabPressScreen);view.liftedTab.reset();tabPressTick=GetTickCount();
                        if(view.tabs.size()>1)SetTimer(hwnd,TabPressTimer,TabPressMilliseconds,nullptr);
                    }
                    if(ItemHit(hit)){
                        const bool control=(w&MK_CONTROL)!=0,shift=(w&MK_SHIFT)!=0;
                        clickCollapse=!control&&!shift&&interaction.IsSelected(Collection()->items[hit.index].id);
                        SelectFocus(hit.index,control,shift,true);titleFocused=false;
                    }
                    SetCapture(hwnd);SetFocus(hwnd);window.Paint();return 0;
                }
                const auto point=ClientPoint(hwnd,l);
                if(InViewport(point)&&!Collapsed()){
                    pressPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};pointer=point;dragging=false;lastClick=0;
                    marqueeBase=(w&MK_CONTROL)?interaction.Selected():std::vector<std::string>{};
                    if(!(w&MK_CONTROL)){interaction.Clear();view.selected.reset();SyncSelection();}
                    marqueeStartScroll=view.scroll;
                    marquee=WidgetRect{point.x,point.y,0,0};SetCapture(hwnd);SetFocus(hwnd);window.Paint();return 0;
                }
            }
            if(message==WM_MOUSEMOVE){
                pointer=ClientPoint(hwnd,l);
                if(marquee){MarqueeSelection(*pointer);window.Paint();return 0;}
                const auto hit=hitAt();
                if(pressed&&pressed->kind==OrganizerHit::Kind::Tab&&view.liftedTab){
                    // A lifted tab follows the cursor with a card that says what
                    // releasing will do: reorder, detach or merge.
                    dragging=true;TrackLiftedTab(hwnd,l);SetCursor(LoadCursorW(nullptr,IDC_SIZEALL));
                    window.Paint();return 0;
                }
                const auto tabHover=hit.kind==OrganizerHit::Kind::Tab?std::optional<std::size_t>(hit.index):std::nullopt;
                if(view.hoverTab!=tabHover){view.hoverTab=tabHover;window.Paint();}
                // While a tab press may still become a long press, tolerate hand
                // movement instead of starting the window move at once.
                const bool holding=pressed&&pressed->kind==OrganizerHit::Kind::Tab&&view.tabs.size()>1&&
                    GetTickCount()-tabPressTick<TabPressMilliseconds;
                const auto dpi=static_cast<int>(GetDpiForWindow(hwnd)?GetDpiForWindow(hwnd):96);
                const int slopX=holding?MulDiv(TabPressSlop,dpi,96):GetSystemMetrics(SM_CXDRAG);
                const int slopY=holding?MulDiv(TabPressSlop,dpi,96):GetSystemMetrics(SM_CYDRAG);
                if(pressed&&(std::abs(GET_X_LPARAM(l)-pressPoint.x)>slopX||std::abs(GET_Y_LPARAM(l)-pressPoint.y)>slopY)){
                    dragging=true;lastClick=0;
                    const bool header=pressed->kind==OrganizerHit::Kind::Title||pressed->kind==OrganizerHit::Kind::Header||
                        pressed->kind==OrganizerHit::Kind::Tab;
                    if(header&&!Locked()){MoveWindow(hwnd,l);return 0;}
                    if(header){KillTimer(hwnd,TabPressTimer);pressed.reset();ReleaseCapture();return 0;}
                    if(pressed->kind==OrganizerHit::Kind::Item&&!view.filtered&&!interaction.Dragging()){
                        dragPrimary=Collection()->items[pressed->index].id;
                        dragOriginal=ItemIds();interaction.BeginDrag(dragOriginal,dragPrimary);SyncSelection();
                        const auto layout=Layout();
                        const auto found=std::find_if(layout.cells.begin(),layout.cells.end(),[&](const auto& cell){return cell.index==pressed->index;});
                        const auto start=ClientPoint(hwnd,MAKELPARAM(pressPoint.x,pressPoint.y));
                        dragOffset=found!=layout.cells.end()?OrganizerPoint{start.x-found->bounds.x,start.y-found->bounds.y}:OrganizerPoint{};
                    }
                }
                if(interaction.Dragging()){
                    // Leaving the window drags the files to other programs, as
                    // from the desktop; the arrangement inside is left as it was.
                    // Released over the desktop itself, the icons leave the organizer.
                    POINT screen{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(hwnd,&screen);
                    RECT frame{};GetWindowRect(hwnd,&frame);
                    if(!PtInRect(&frame,screen)){
                        // Over the desktop the icons stay with us, as on the Dock;
                        // over a program they are handed to it as files.
                        if(owner.overDesktop(screen)){ShowRemoveGhost(screen);return 0;}
                        HideRemoveGhost();
                        std::vector<std::wstring> paths;const auto ids=interaction.Dragged();
                        if(const auto* c=Collection())
                            for(const auto& dragId:ids)
                                for(const auto& item:c->items)if(item.id==dragId){paths.push_back(Wide(item.path));break;}
                        CancelDrag();hover.reset();
                        if(owner.drag(hwnd,paths)&&!owner.stopped&&!stopping)RemoveIds(ids);
                        return 0;
                    }
                    HideRemoveGhost();
                    PreviewDrag(*pointer);hover.reset();window.Paint();return 0;
                }
                const auto next=ItemHit(hit)?std::optional<std::size_t>(hit.index):std::nullopt;
                hover=next;window.Paint();
                return {};
            }
            if(message==WM_CAPTURECHANGED){
                // A release reads the insertion slot before releasing capture.
                if(!pressed){HideTabGhost();owner.EndMergeHighlight();}
                if(interaction.Dragging()||marquee)CancelDrag();else{pressed.reset();dragging=false;}
                return {};
            }
            if(message==WM_MOUSELEAVE){hover.reset();view.hoverTab.reset();if(!interaction.Dragging()&&!marquee)pointer.reset();window.Paint();return {};}
            if(message==WM_LBUTTONUP&&marquee){
                MarqueeSelection(ClientPoint(hwnd,l));marquee.reset();marqueeBase.clear();
                if(GetCapture()==hwnd)ReleaseCapture();
                window.Paint();return 0;
            }
            if(message==WM_LBUTTONUP&&pressed){
                const auto hit=hitAt(),down=*pressed;const bool moved=dragging;
                if(interaction.Dragging()&&RemoveGhostShown()){
                    const auto ids=interaction.Dragged();CancelDrag();RemoveIds(ids);lastClick=0;return 0;
                }
                if(interaction.Dragging()){
                    pointer=ClientPoint(hwnd,l);PreviewDrag(*pointer);CommitDrag(InViewport(*pointer));lastClick=0;return 0;
                }
                const auto slot=view.tabInsert;
                pressed.reset();dragging=false;if(GetCapture()==hwnd)ReleaseCapture();
                KillTimer(hwnd,TabPressTimer);
                if(down.kind==OrganizerHit::Kind::Tab&&view.liftedTab){
                    const auto from=*view.liftedTab;const auto tabId=TabId(from);view.liftedTab.reset();lastClick=0;
                    HideTabGhost();
                    POINT cursor{};GetCursorPos(&cursor);owner.EndMergeHighlight();window.Paint();
                    if(tabId.empty())return 0;
                    if(const auto* target=owner.MergeTargetAt(*this,cursor))owner.RequestMerge(tabId,target->id,true);
                    else if(slot)owner.MoveTab(tabId,*slot>from?*slot-1:*slot);
                    else if(!WindowContains(cursor))owner.Detach(*this,tabId,cursor);
                    return 0;
                }
                if(moved){
                    return 0;
                }
                if(!SameHit(hit,down)){lastClick=0;return 0;}
                if(hit.kind==OrganizerHit::Kind::Item&&clickCollapse){SelectFocus(hit.index);window.Paint();}
                if(hit.kind==OrganizerHit::Kind::Item&&(w&(MK_CONTROL|MK_SHIFT))){lastClick=0;return 0;}
                if(hit.kind==OrganizerHit::Kind::Search){lastClick=0;ToggleSearch();return 0;}
                if(hit.kind==OrganizerHit::Kind::Header){lastClick=0;return 0;}
                if(hit.kind==OrganizerHit::Kind::Tab&&TabId(hit.index)!=ContentId()){
                    lastClick=GetTickCount();lastHit=hit;lastPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                    Activate(hit.index);return 0;
                }
                // Also works with hosts registered before CS_DBLCLKS was enabled.
                const DWORD now=GetTickCount();
                const bool twice=lastClick&&SameHit(hit,lastHit)&&now-lastClick<=GetDoubleClickTime()&&
                    std::abs(GET_X_LPARAM(l)-lastPoint.x)<=GetSystemMetrics(SM_CXDOUBLECLK)&&
                    std::abs(GET_Y_LPARAM(l)-lastPoint.y)<=GetSystemMetrics(SM_CYDOUBLECLK);
                lastClick=now;lastHit=hit;lastPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                if(twice){
                    lastClick=0;
                    if(hit.kind==OrganizerHit::Kind::Item)Open(hit.index);
                    else if(hit.kind==OrganizerHit::Kind::Title||hit.kind==OrganizerHit::Kind::Tab)BeginEditor(TitleEditor);
                }
                return 0;
            }
            if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){
                if(w==VK_ESCAPE&&(interaction.Dragging()||marquee||pressed)){CancelDrag();return 0;}
                if(interaction.Dragging()||marquee)return 0;
                if(w==VK_ESCAPE&&view.searchOpen){CloseSearch();return 0;}
                if(w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){
                    interaction.SelectAll(ItemIds(true));SyncSelection();if(!view.selection.empty())view.selected=view.selection.front();
                    window.Paint();return 0;
                }
                if(w=='F'&&(GetKeyState(VK_CONTROL)&0x8000)){if(!view.searchOpen)ToggleSearch();else if(editor)SetFocus(editor);return 0;}
                if(w==VK_APPS||(w==VK_F10&&(GetKeyState(VK_SHIFT)&0x8000))){Menu(MAKELPARAM(-1,-1));return 0;}
                if((w==VK_RETURN||w==VK_F2)&&titleFocused){BeginEditor(TitleEditor);return 0;}
                if((w==VK_RETURN||w==VK_SPACE)&&view.selected){Open(*view.selected);return 0;}
                if(w==VK_DELETE&&!interaction.Selected().empty()){RemoveSelection();return 0;}
                if(w==VK_TAB&&(GetKeyState(VK_CONTROL)&0x8000)){CycleTab((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
                if(w==VK_TAB){
                    const auto visible=VisibleIndices();
                    if(!view.selected&&!titleFocused)titleFocused=true;
                    else if(titleFocused){titleFocused=false;if(!visible.empty())view.selected=visible.front();}
                    else{
                        const auto found=std::find(visible.begin(),visible.end(),*view.selected);
                        if(found!=visible.end()&&found+1!=visible.end())view.selected=*(found+1);
                        else{view.selected.reset();titleFocused=true;}
                    }
                    if(view.selected)SelectFocus(*view.selected);else{interaction.Clear();SyncSelection();}
                    RevealSelection();window.Paint();return 0;
                }
                if((w==VK_LEFT||w==VK_RIGHT||w==VK_UP||w==VK_DOWN)&&view.selected){
                    const auto* c=Collection();const auto selected=*view.selected;
                    const bool back=w==VK_LEFT||w==VK_UP;
                    const bool alt=(GetKeyState(VK_MENU)&0x8000)||
                        (message==WM_SYSKEYDOWN&&(l&(static_cast<LPARAM>(1)<<29)));
                    if(alt&&!view.filtered&&
                        (w==VK_LEFT||w==VK_RIGHT||Collapsed())){
                        if((back&&selected>0)||(!back&&selected+1<c->items.size())){
                            std::vector<std::string> ids;for(const auto& item:c->items)ids.push_back(item.id);
                            const auto next=back?selected-1:selected+1;std::swap(ids[selected],ids[next]);
                            const auto content=ContentId();
                            const bool ok=owner.Apply([&](auto& state){organizer::Reorder(*organizer::Find(state,content),ids);});
                            if(ok)SelectFocus(next);
                            owner.Publish(!ok);
                        }
                    }else{
                        const auto visible=VisibleIndices();
                        const auto found=std::find(visible.begin(),visible.end(),selected);
                        if(found!=visible.end()){
                            if(back&&found!=visible.begin())SelectFocus(*(found-1),false,(GetKeyState(VK_SHIFT)&0x8000)!=0);
                            else if(!back&&found+1!=visible.end())SelectFocus(*(found+1),false,(GetKeyState(VK_SHIFT)&0x8000)!=0);
                        }
                    }
                    RevealSelection();window.Paint();return 0;
                }
            }
            if(message==WM_DROPFILES){
                const auto drop=reinterpret_cast<HDROP>(w);
                struct FinishDrop {HDROP handle;~FinishDrop(){DragFinish(handle);}} finish{drop};
                const UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
                bool valid=count<=1024;std::vector<std::string> paths;if(valid)paths.reserve(count);
                for(UINT i=0;valid&&i<count;++i){
                    const UINT length=DragQueryFileW(drop,i,nullptr,0);
                    if(!length||length>32767){valid=false;break;}
                    std::wstring value(length+1,L'\0');
                    if(DragQueryFileW(drop,i,value.data(),length+1)!=length){valid=false;break;}
                    value.resize(length);const auto text=OrganizerUtf8(value);
                    if(text.empty()){valid=false;break;}paths.push_back(text);
                }
                const bool ok=valid&&owner.AddPaths(ContentId(),paths);if(!valid)owner.error="invalid";
                owner.Publish(!ok);return 0;
            }
            return {};
        }
        Impl& owner;std::string id,shownId;OrganizerViewState view;
        POINT tabPressScreen{};bool windowMoving=false;DWORD tabPressTick=0;
        HWND tabGhost=nullptr;LayeredSurface ghostSurface;int ghostState=-1;
        HWND removeGhost=nullptr;LayeredSurface removeGhostSurface;bool removeGhostDrawn=false;
        std::optional<std::size_t> hover;std::optional<OrganizerHit> pressed;
        POINT pressPoint{},lastPoint{};DWORD lastClick=0;OrganizerHit lastHit{};
        bool stopping=false,dragging=false,titleFocused=false,menu=false,composing=false,clickCollapse=false;
        organizer::Interaction interaction;OrganizerMotion motion;OrganizerDropTarget dropTarget;HWND dropWindow=nullptr;
        std::optional<OrganizerPoint> pointer;OrganizerPoint dragOffset{};std::string dragPrimary;
        std::optional<WidgetRect> marquee;std::vector<std::string> marqueeBase,dragOriginal;float marqueeStartScroll=0;
        bool reducedMotion=false,motionTimer=false,externalDrop=false;std::size_t externalCount=0;
        ULONGLONG lastMotionTick=0,lastScrollTick=0;
        HWND editor=nullptr;UINT editorKind=0;WNDPROC editorProcedure=nullptr;HFONT font=nullptr;
        unsigned editorDpi=0;UINT editorCaretInterval=0;COLORREF editorMatte=0;
        bool editorPaintPending=false,editorPainting=false,editorCaretVisible=true;
        OrganizerIconCache icons{OrganizerIconCache::Loader{},true};std::atomic_bool iconUpdatePosted{false};NativeWidgetWindow window;
    };
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,settings::IFileStore* files,OpenTarget open,DragTargets drag,bool automaticDesktop,std::vector<std::wstring> desktopRoots):
        instance(instance),path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),open(std::move(open)),drag(std::move(drag)),
        store(files?*files:localFiles,PathKey),loaded(store.Load(this->path)),state(std::move(loaded.value)),automaticDesktop(automaticDesktop),
        desktopSource(instance,[this](auto snapshot){SynchronizeDesktop(std::move(snapshot));},std::move(desktopRoots)){
        if(automaticDesktop&&loaded.state==organizer::LoadState::Fresh)state=organizer::DesktopDefaults();
        if(loaded.state==organizer::LoadState::Fresh||loaded.state==organizer::LoadState::Failed)
            for(auto& entry:state.collections)entry.enabled=false;
        // Earlier builds hid closed boxes with no way back; drop the empty ones.
        // The cleaned state is persisted by the next successful Apply.
        else organizer::PruneClosedEmpty(state);
        if(automaticDesktop&&loaded.state==organizer::LoadState::Fresh)
            for(auto& collection:state.collections)if(collection.desktopRole==organizer::DesktopRole::Files)collection.enabled=true;
        // Retire the old fixed-size compact mode without discarding saved bounds.
        for(auto& collection:state.collections)collection.mode=organizer::Mode::Expanded;
        if(!this->open)this->open=[](HWND hwnd,const std::wstring& target){
            (void)hwnd;
            return OpenOrganizerTarget(target);
        };
        if(!this->drag)this->drag=[](HWND hwnd,const std::vector<std::wstring>& paths){return DragPathsOutToDesktop(hwnd,paths).desktop;};
    }
    ~Impl(){Shutdown();}
    HWND Handle(std::string_view id)const{
        const auto found=entries.find(std::string(id));return found==entries.end()?nullptr:found->second->window.Handle();
    }
    bool Running()const{
        return std::any_of(entries.begin(),entries.end(),[](const auto& entry){return entry.second->window.Handle()!=nullptr;});
    }
    WidgetMaterialState MaterialState()const{
        auto materialState=WidgetMaterialState::Detached;
        for(const auto& entry:entries){
            if(!entry.second->window.Handle())continue;
            const auto material=entry.second->window.MaterialState();
            // A ready collection must not hide another live collection's
            // fallback or startup. Inactive is reserved for no live windows.
            if(material==WidgetMaterialState::TransparentFallback)return material;
            if(material!=WidgetMaterialState::Ready)materialState=WidgetMaterialState::Starting;
            else if(materialState==WidgetMaterialState::Detached)materialState=WidgetMaterialState::Ready;
        }
        return materialState;
    }
    std::wstring State(bool applied=false,bool saved=false)const{
        const auto* item=organizer::Find(state,organizer::DefaultCollectionId);
        // Window state lives on the host, which differs when default is a tab.
        const auto* host=organizer::Host(state,organizer::DefaultCollectionId);if(!host)host=item;
        const bool collapsed=host&&host->mode==organizer::Mode::Collapsed;
        // Report the size the window actually uses, never a raw stored value
        // the page would reject (the bridge requires the clamped ranges).
        const int width=collapsed?180:std::clamp(host&&host->bounds?host->bounds->width:420,ExpandedMinWidth,ExpandedMaxWidth);
        const int height=collapsed?216:std::clamp(host&&host->bounds?host->bounds->height:392,
            ExpandedMinHeight,ExpandedMaxHeight);
        const auto material=MaterialState();
        const char* status=material==WidgetMaterialState::Ready?"native-ready":material==WidgetMaterialState::Starting?"native-starting":
            material==WidgetMaterialState::Detached?"inactive":"transparent-fallback";
        nlohmann::json collections=nlohmann::json::array();
        for(const auto& c:state.collections){
            const auto* owner=organizer::Host(state,c.id);
            collections.push_back({{"id",c.id},{"title",c.title},{"enabled",c.enabled},
                {"running",owner&&Handle(owner->id)!=nullptr},{"count",c.items.size()}});
        }
        nlohmann::json value={{"version",1},{"type","organizer-state"},{"enabled",item?item->enabled:false},
            {"running",Running()},{"locked",host?host->locked:false},{"material",host&&host->frosted?"frosted-glass":"liquid-glass"},
            {"materialStatus",status},
            {"loadStatus",loaded.state==organizer::LoadState::Loaded?"loaded":loaded.state==organizer::LoadState::Recovered?"recovered":
                loaded.state==organizer::LoadState::Failed?"failed":"fresh"},{"error",error},{"width",width},
            {"height",height},{"result",applied?"applied":"snapshot"},{"saved",saved},
            {"title",item?item->title:"桌面收纳"},{"count",item?item->items.size():0},{"mode",collapsed?"collapsed":"expanded"},
            {"layout",host&&host->layout==organizer::Layout::List?"list":"icons"},{"collections",std::move(collections)}};
        value["iconScale"]=host?host->iconScale:70;
        return Wide(value.dump());
    }
    void Publish(bool failed){if(notify&&!stopped)notify(State(),failed);}
    void PlaceDesktopDefaults(){
        if(!automaticDesktop||desktopDefaultsPlaced||loaded.state!=organizer::LoadState::Fresh)return;
        auto* applications=organizer::Find(state,organizer::DefaultCollectionId);
        organizer::Collection* documents=nullptr;
        for(auto& collection:state.collections)if(collection.desktopRole==organizer::DesktopRole::Files)documents=&collection;
        if(!applications||!documents)return;
        const auto monitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);MONITORINFO info{};info.cbSize=sizeof(info);
        if(!GetMonitorInfoW(monitor,&info))return;
        UINT xDpi=96,yDpi=96;if(FAILED(GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&xDpi,&yDpi))||!xDpi)xDpi=96;
        const double fit=widget_placement::MonitorFitScale(monitor);
        const int width=window::FitLength(260,fit,ExpandedMinWidth);
        const int height=std::max(ExpandedMinHeight,std::min(window::FitLength(680,fit,ExpandedMinHeight),
            MulDiv(info.rcWork.bottom-info.rcWork.top,96,static_cast<int>(xDpi))-64));
        const auto px=[&](int value){return MulDiv(value,static_cast<int>(xDpi),96);};
        const auto start=widget_placement::FirstPosition(nullptr,monitor,info.rcWork,{px(width),px(height)},xDpi);
        applications->bounds=organizer::Bounds{start.x,start.y,width,height};
        documents->bounds.reset();
        desktopDefaultsPlaced=true;
    }
    bool Runtime(){
        organizer::SyncGroups(state);
        const auto* root=organizer::Find(state,organizer::DefaultCollectionId);const bool enabled=root&&root->enabled;
        if(!enabled)desktopSource.Stop();
        for(auto it=entries.begin();it!=entries.end();){
            const auto* c=organizer::Find(state,it->first);const bool host=c&&c->parent.empty();
            if(!enabled||!host||!c->enabled)it->second->Stop();
            // A merged or deleted host may be the caller further up this
            // window's stack; keep the object alive until the next Apply.
            if(!host){retired.push_back(std::move(it->second));it=entries.erase(it);}else ++it;
        }
        if(!enabled)return true;
        PlaceDesktopDefaults();
        for(const auto& collection:state.collections){
            if(!collection.enabled||!collection.parent.empty())continue;
            auto& entry=entries[collection.id];if(!entry)entry=std::make_unique<Entry>(*this,collection.id);
            if(!entry->Configure())return false;
        }
        if(automaticDesktop&&!desktopSource.Start())return false;
        return true;
    }
    bool Restore(){if(stopped)return false;
        if(automaticDesktop)organizer::EnsureDesktopCollections(state);
        applying=true;const bool ok=Runtime();applying=false;if(!ok){desktopSource.Stop();error="runtime";for(auto& pair:entries)pair.second->Stop();}return ok;}
    void SynchronizeDesktop(organizer::DesktopSnapshot snapshot){
        if(stopped||applying||!automaticDesktop||!snapshot.complete||!Running())return;
        for(const auto& [id,entry]:entries){
            (void)id;
            if(entry->interaction.Dragging()||entry->marquee||entry->windowMoving||entry->editor||entry->menu)return;
        }
        auto next=state;const auto result=organizer::SyncDesktop(next,snapshot,PathKey);
        if(result.changed){
            const bool ok=Apply([&](auto& current){
                // Preserve current geometry captured by Apply; only references
                // and automatic collection roles come from the source snapshot.
                for(auto& collection:next.collections){
                    if(const auto* previous=organizer::Find(current,collection.id))collection.bounds=previous->bounds;
                }
                current=std::move(next);
            });
            if(ok&&result.limited)error="limit";
            if(notify)notify(State(),false);
        }else if(result.limited){error="limit";if(notify)notify(State(),false);}
    }
    // The window's live size is the expanded size only while it is shown
    // expanded; a collapsed or temporarily expanded window updates position.
    static organizer::Bounds Stored(const organizer::Collection& c,const Entry& entry,const widget::Preferences& live){
        const bool fixed=c.mode==organizer::Mode::Collapsed||entry.view.temporaryExpanded;
        const auto previous=c.bounds.value_or(organizer::Bounds{});
        // Collapsed and temporary-search windows keep the user's expanded
        // geometry. Normalize legacy heights below the new one-column
        // envelope before persisting so a restart cannot restore an unusable
        // 160 DIP window.
        return {live.x,live.y,
            fixed?std::clamp(previous.width,ExpandedMinWidth,ExpandedMaxWidth):live.width,
            fixed?std::clamp(previous.height,ExpandedMinHeight,ExpandedMaxHeight):live.height};
    }
    void CaptureBounds(){
        for(auto& [id,entry]:entries)if(entry->window.Handle()){
            auto* c=organizer::Find(state,id);if(!c||!c->parent.empty())continue;
            c->bounds=Stored(*c,*entry,entry->window.Bounds());
        }
    }
    bool Apply(const std::function<void(organizer::State&)>& update){
        if(stopped||applying)return false;
        struct SelectionSnapshot{organizer::Interaction interaction;std::optional<std::size_t> focus;};
        std::map<std::string,SelectionSnapshot> selections;
        for(const auto& [id,entry]:entries)selections.emplace(id,SelectionSnapshot{entry->interaction,entry->view.selected});
        retired.clear();
        applying=true;CaptureBounds();const auto previous=state;update(state);
        const bool runtime=Runtime();
        if(runtime)CaptureBounds();
        if(!runtime||!store.Save(path,state)){
            state=previous;const bool rollback=Runtime();error=rollback?(runtime?"save":"runtime"):"rollback";
            if(!rollback)for(auto& pair:entries)pair.second->Stop();
            else for(auto& [id,entry]:entries){
                const auto found=selections.find(id);if(found==selections.end())continue;
                entry->interaction=found->second.interaction;entry->interaction.EndDrag();
                entry->view.selected=found->second.focus;entry->Filter();entry->window.Paint();
            }
            applying=false;return false;
        }
        loaded.state=organizer::LoadState::Loaded;error.clear();applying=false;return true;
    }
    bool AddPaths(std::string_view collectionId,const std::vector<std::string>& paths){
        auto* collection=organizer::Find(state,collectionId);
        if(stopped||applying||!collection||paths.size()>1024)return false;
        std::vector<organizer::Item> items;items.reserve(std::min<std::size_t>(paths.size(),organizer::MaxItems));
        // One unusable path no longer rejects the batch: add the rest and
        // report the first problem.
        std::string failure;
        std::set<std::string> keys;for(const auto& item:collection->items)keys.insert(PathKey(item.path));
        for(const auto& input:paths){
            const auto normalized=organizer::NormalizeLocalPath(input);
            if(!normalized){if(failure.empty())failure="invalid";continue;}
            if(!keys.insert(PathKey(*normalized)).second)continue;
            if(collection->items.size()+items.size()>=organizer::MaxItems){if(failure.empty())failure="limit";break;}
            const auto wide=Wide(*normalized);const auto attributes=GetFileAttributesW(wide.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES){if(failure.empty())failure="target";continue;}
            auto kind=(attributes&FILE_ATTRIBUTE_DIRECTORY)?organizer::ItemKind::Folder:organizer::ItemKind::File;
            if(kind==organizer::ItemKind::File&&PathKey(std::filesystem::u8path(*normalized).extension().u8string())==".lnk")kind=organizer::ItemKind::Shortcut;
            const auto id=CreateUuid();if(id.empty()){error="runtime";return false;}
            items.push_back({id,*normalized,{},kind});
        }
        auto next=*collection;const auto change=organizer::AddItems(next,items,PathKey);
        if(change==organizer::Change::Invalid||change==organizer::Change::Limit){error=change==organizer::Change::Limit?"limit":"invalid";return false;}
        bool ok=true;
        // The model copy contains only collection changes. Preserve live bounds
        // captured by Apply instead of replacing all concurrent window state.
        if(change==organizer::Change::Changed)
            ok=Apply([&](auto& value){organizer::Find(value,collectionId)->items=std::move(next.items);});
        if(ok&&!failure.empty()){error=failure;return false;}
        return ok;
    }
    void Create(Entry& source){
        const auto id=CreateUuid();if(id.empty()){error="runtime";Publish(true);return;}
        const auto bounds=source.window.Bounds();const auto* collection=source.Collection();
        const bool frosted=collection&&collection->frosted;
        const bool ok=Apply([&](auto& value){
            const auto change=organizer::CreateCollection(value,id,u8"新建收纳",
                organizer::Bounds{bounds.x+32,bounds.y+32,420,392});
            if(change==organizer::Change::Changed)organizer::Find(value,id)->frosted=frosted;
        });
        if(ok&&!organizer::Find(state,id)){error="limit";Publish(true);}else Publish(!ok);
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        if(stopped||applying)return {};
        if(command==ui::PageCommand::Ready)return State();
        if(!organizer::Find(state,organizer::DefaultCollectionId))return {};
        if(command>=ui::PageCommand::OrganizerIconScaleFirst&&command<=ui::PageCommand::OrganizerIconScaleLast){
            const int scale=50+static_cast<int>(command)-static_cast<int>(ui::PageCommand::OrganizerIconScaleFirst);
            const bool saved=Apply([&](auto& s){for(auto& c:s.collections)c.iconScale=scale;});
            return State(true,saved);
        }
        if(command==ui::PageCommand::OrganizerReset){
            const auto entry=entries.find(organizer::DefaultCollectionId);
            if(entry!=entries.end())entry->second->CancelDrag();
        }
        bool handled=false;
        switch(command){
            case ui::PageCommand::OrganizerEnable:handled=Apply([&](auto& s){if(automaticDesktop)organizer::EnsureDesktopCollections(s);organizer::Find(s,organizer::DefaultCollectionId)->enabled=true;});break;
            case ui::PageCommand::OrganizerDisable:handled=Apply([](auto& s){organizer::Find(s,organizer::DefaultCollectionId)->enabled=false;});break;
            case ui::PageCommand::OrganizerLiquidGlass:handled=Apply([](auto& s){for(auto& c:s.collections)c.frosted=false;});break;
            case ui::PageCommand::OrganizerFrostedGlass:handled=Apply([](auto& s){for(auto& c:s.collections)c.frosted=true;});break;
            case ui::PageCommand::OrganizerReset:handled=Apply([&](auto& s){
                organizer::Reset(*organizer::Find(s,organizer::DefaultCollectionId));
                if(automaticDesktop)organizer::RestoreDesktopCollections(s);});break;
            default:return {};
        }
        return State(true,handled);
    }
    void Native(Entry& entry,WidgetAction action,const widget::Preferences& bounds){
        if(stopped||applying)return;
        if(action==WidgetAction::Settings){if(openSettings)openSettings();return;}
        if(!entry.Collection()||action==WidgetAction::Refresh)return;
        if(action==WidgetAction::Close){Close(entry);return;}
        if(action==WidgetAction::Reset){entry.CancelDrag();entry.FinishTitle(false);entry.CloseSearch();entry.view.temporaryExpanded=false;}
        const bool ok=Apply([&](auto& s){
            // Window actions belong to the host, whichever tab is visible.
            auto* c=organizer::Find(s,entry.id);
            switch(action){
                case WidgetAction::Close:c->enabled=false;break;
                case WidgetAction::Lock:c->locked=!c->locked;break;
                case WidgetAction::Liquid:for(auto& other:s.collections)other.frosted=false;break;
                case WidgetAction::Frosted:for(auto& other:s.collections)other.frosted=true;break;
                case WidgetAction::Bounds:c->bounds=Stored(*c,entry,bounds);break;
                case WidgetAction::Reset:organizer::Reset(*c);break;
                case WidgetAction::Settings:case WidgetAction::Refresh:break;
            }
        });
        Publish(!ok);
    }
    Entry* MergeTargetAt(const Entry& source,POINT screen){
        for(auto& [key,entry]:entries)if(entry.get()!=&source&&entry->HeaderContains(screen))return entry.get();
        return nullptr;
    }
    void HighlightMerge(const Entry& source,POINT screen){
        const auto* target=MergeTargetAt(source,screen);
        for(auto& [key,entry]:entries){
            const bool on=entry.get()==target;
            if(entry->view.mergeTarget!=on){entry->view.mergeTarget=on;entry->window.Paint();}
        }
    }
    void EndMergeHighlight(){
        for(auto& [key,entry]:entries)if(entry->view.mergeTarget){entry->view.mergeTarget=false;entry->window.Paint();}
    }
    // Runs from the target window's queue so the source window is never
    // destroyed while its own message handler is still on the stack.
    void RequestMerge(std::string source,std::string target,bool tabOnly){
        const auto hwnd=Handle(target);
        pendingMerge=PendingMerge{std::move(source),std::move(target),tabOnly};
        if(!hwnd||!PostMessageW(hwnd,WM_COMMAND,MergeCommand,0))pendingMerge.reset();
    }
    void FinishMerge(){
        if(!pendingMerge||stopped||applying)return;
        const auto merge=*pendingMerge;pendingMerge.reset();
        const bool ok=Apply([&](auto& s){
            // A single dragged tab leaves its group first, then joins.
            if(merge.tabOnly&&organizer::Tabs(s,organizer::Host(s,merge.source)?organizer::Host(s,merge.source)->id:merge.source).size()>1)
                organizer::DetachTab(s,merge.source,organizer::Bounds{});
            organizer::MergeGroup(s,merge.source,merge.target);
        });
        Publish(!ok);
    }
    // Reorders a tab within its group. When the front tab changes the host id
    // changes too; the window object is re-keyed so it is kept, not rebuilt.
    void MoveTab(const std::string& tabId,std::size_t to){
        const auto* host=organizer::Host(state,tabId);if(!host)return;
        const std::string oldHost=host->id;
        const bool ok=Apply([&](auto& s){
            if(organizer::MoveTab(s,tabId,to)!=organizer::Change::Changed)return;
            const auto* next=organizer::Host(s,tabId);
            if(!next||next->id==oldHost)return;
            auto node=entries.extract(oldHost);
            if(node.empty())return;
            node.key()=next->id;node.mapped()->id=next->id;entries.insert(std::move(node));
        });
        Publish(!ok);
    }
    void Detach(Entry& entry,const std::string& tabId,POINT cursor){
        const auto dpi=static_cast<int>(GetDpiForWindow(entry.window.Handle()));
        const auto size=entry.window.Bounds();
        const organizer::Bounds bounds{cursor.x-MulDiv(80,dpi?dpi:96,96),cursor.y-MulDiv(20,dpi?dpi:96,96),
            std::clamp(size.width,ExpandedMinWidth,ExpandedMaxWidth),
            std::clamp(size.height,ExpandedMinHeight,ExpandedMaxHeight)};
        const bool ok=Apply([&](auto& s){organizer::DetachTab(s,tabId,bounds);});
        Publish(!ok);
    }
    // Closing the default collection hides every box as before. Any other
    // box is deleted (after confirmation when it holds items); its icons can
    // first be moved to the default box. Source files are never touched.
    void Close(Entry& entry){
        if(stopped||applying)return;
        const auto* tab=entry.Collection();if(!tab)return;
        const std::string tabId=tab->id,hostId=entry.id;
        if(tabId==organizer::DefaultCollectionId){
            const bool ok=Apply([](auto& s){organizer::Find(s,organizer::DefaultCollectionId)->enabled=false;});
            Publish(!ok);return;
        }
        int choice=IDNO;
        if(!tab->items.empty()){
            const auto* root=organizer::Find(state,organizer::DefaultCollectionId);
            const auto target=Wide(root?root->title:u8"桌面收纳");
            const auto text=L"删除收纳“"+Wide(tab->title)+L"”？\n\n其中有 "+std::to_wstring(tab->items.size())+
                L" 个图标，原文件不会被删除。\n\n是：把图标移到“"+target+L"”后删除\n否：直接删除\n取消：保留这个收纳";
            choice=MessageBoxW(entry.window.Handle(),text.c_str(),L"桌面收纳",MB_YESNOCANCEL|MB_ICONQUESTION);
            if(choice==IDCANCEL)return;
            const auto found=entries.find(hostId);
            if(found==entries.end()||found->second.get()!=&entry||!organizer::Find(state,tabId))return;
        }
        entry.CancelDrag();entry.FinishTitle(false);entry.CloseSearch();
        bool full=false;
        const bool ok=Apply([&](auto& s){
            if(choice==IDYES){
                std::vector<std::string> ids;for(const auto& item:organizer::Find(s,tabId)->items)ids.push_back(item.id);
                if(organizer::MoveItems(s,tabId,organizer::DefaultCollectionId,ids,PathKey)!=organizer::Change::Changed){full=true;return;}
            }
            organizer::DeleteTab(s,tabId);
        });
        if(ok&&full){error="limit";Publish(true);return;}
        Publish(!ok);
    }
    void Shutdown(){if(stopped)return;stopped=true;desktopSource.Stop();pendingMerge.reset();for(auto& pair:entries)pair.second->Stop();entries.clear();retired.clear();}
    HINSTANCE instance;std::string path,error;Notify notify;std::function<void()> openSettings;OpenTarget open;DragTargets drag;
    LocalSettingsFileStore localFiles;organizer::Store store;organizer::LoadedState loaded;organizer::State state;
    std::map<std::string,std::unique_ptr<Entry>> entries;bool stopped=false,applying=false;
    std::vector<std::unique_ptr<Entry>> retired;
    struct PendingMerge{std::string source,target;bool tabOnly=false;};
    std::optional<PendingMerge> pendingMerge;
    const bool automaticDesktop;
    // Outside the window, anything but a program window or another organizer
    // keeps the drag here with "移除"; the first point past the edge is often
    // over the clock or the window's own clear margin, not the wallpaper.
    std::function<bool(POINT)> overDesktop=[](POINT screen){return !OverProgram(screen);};
    bool desktopDefaultsPlaced=false;
    DesktopOrganizerSource desktopSource;
};
OrganizerSession::OrganizerSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,
    settings::IFileStore* files,OpenTarget open,DragTargets drag,bool automaticDesktop,std::vector<std::wstring> desktopRoots):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),files,std::move(open),std::move(drag),automaticDesktop,std::move(desktopRoots))){}
OrganizerSession::~OrganizerSession()=default;
bool OrganizerSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> OrganizerSession::HandlePage(ui::PageCommand c){return impl_->HandlePage(c);}
std::wstring OrganizerSession::State()const{return impl_->State();}
void OrganizerSession::Shutdown(){impl_->Shutdown();}
bool OrganizerSession::Running()const{return impl_->Running();}
HWND OrganizerSession::Handle()const{return Handle(organizer::DefaultCollectionId);}
HWND OrganizerSession::Handle(std::string_view id)const{return impl_->Handle(id);}
bool OrganizerSession::AddPaths(const std::vector<std::string>& paths){return AddPaths(organizer::DefaultCollectionId,paths);}
bool OrganizerSession::AddPaths(std::string_view id,const std::vector<std::string>& paths){return impl_->AddPaths(id,paths);}
const organizer::State& OrganizerSession::Snapshot()const{return impl_->state;}
void OrganizerSession::SynchronizeDesktop(organizer::DesktopSnapshot snapshot){impl_->SynchronizeDesktop(std::move(snapshot));}
bool OrganizerSession::MergeAtCursorForTest(std::string_view id){
    const auto found=impl_->entries.find(std::string(id));if(found==impl_->entries.end())return false;
    POINT cursor{};GetCursorPos(&cursor);
    const auto* target=impl_->MergeTargetAt(*found->second,cursor);if(!target)return false;
    impl_->RequestMerge(found->first,target->id,false);return impl_->pendingMerge.has_value();
}
void OrganizerSession::SetDesktopProbeForTest(std::function<bool(POINT)> probe){if(probe)impl_->overDesktop=std::move(probe);}
}
