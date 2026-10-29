#pragma once
#include "core/organizer/organizer_model.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/material/material_api.h"
#include <optional>
namespace lume::platform::windows {
struct OrganizerMotionFrame;
struct OrganizerHit {
    // Header: empty title-bar space (or a collapsed body) that only drags.
    enum class Kind { None,Item,Title,Search,Tab,Header };
    Kind kind=Kind::None;
    std::size_t index=0;
};
struct OrganizerViewState {
    bool searchOpen=false,filtered=false,temporaryExpanded=false,titleEditing=false,searchEditing=false;
    std::string query;
    std::vector<std::size_t> visible;
    std::optional<std::size_t> selected;
    float scroll=0;
    // 多选与完整顺序预览；搜索过滤时 visible 优先于 order。
    std::vector<std::size_t> selection;
    std::vector<std::size_t> order;
    // 标签组：两个及以上标题时，标题栏改为可横向滚动的标签条。
    std::vector<std::wstring> tabs;
    std::size_t activeTab=0;
    std::optional<std::size_t> hoverTab,liftedTab,dropTab;
    // While a lifted tab is over its own strip: the slot (0..tabs) it would take.
    std::optional<std::size_t> tabInsert;
    float tabScroll=0;
    // 另一个收纳正被拖到本标题栏上（合并），或图标正被拖入本收纳。
    bool mergeTarget=false,dropTarget=false;
};
struct OrganizerCell {
    std::size_t index=0;
    WidgetRect bounds;
};
struct OrganizerLayout {
    WidgetRect title,search,searchInput,searchClear,viewport;
    std::vector<OrganizerCell> cells;
    // Expanded tab groups only; empty for a single collection.
    WidgetRect tabStrip,header;
    std::vector<WidgetRect> tabs;
    float maxScroll=0,maxTabScroll=0;
    bool collapsed=false;
};
float OrganizerTabWidth(const std::wstring&);
OrganizerLayout LayoutOrganizer(float width,float height,const organizer::Collection&,const OrganizerViewState& = {});
std::vector<LumeMaterialPanel> OrganizerSearchPanels(const OrganizerLayout&,const OrganizerViewState&);
using OrganizerImage=std::function<std::shared_ptr<const WidgetBitmap>(const organizer::Item&)>;
void DrawOrganizer(WidgetCanvas&,float width,float height,const organizer::Collection&,std::optional<std::size_t> hover,
    const OrganizerImage& image={},const OrganizerViewState& = {},const OrganizerMotionFrame* motion=nullptr);
OrganizerHit HitOrganizer(float width,float height,const organizer::Collection&,float x,float y,const OrganizerViewState& = {});
}
