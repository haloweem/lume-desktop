#pragma once
#include "platform/windows/organizer/organizer_view.h"
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace lume::platform::windows {
struct OrganizerPoint { float x=0,y=0; };
struct OrganizerMotionCell {
    std::size_t index=0;
    WidgetRect bounds;
    float scale=1,lift=0;
    bool floating=false;
};
struct OrganizerMotionFrame {
    // index 始终指向本次 Collection；拖动主项排在最后，陪衬最多两个。
    std::vector<OrganizerMotionCell> cells;
    // 来自预览布局的目标格位，不参与命中或重新计算插入位置。
    std::vector<WidgetRect> slots;
    bool external=false;
    std::size_t count=0;
    float feedback=0;
    std::optional<WidgetRect> marquee;
};
class OrganizerMotion {
public:
    // 所有坐标均为 DIP；pointer/offset 直接决定主拖动项左上角，不经过弹簧。
    const OrganizerMotionFrame& Update(const organizer::Collection&,const OrganizerLayout&,
        const std::optional<OrganizerPoint>& pointer,const std::vector<std::string>& dragged,
        std::string_view primary,OrganizerPoint offset,float dt,bool reducedMotion,
        bool external=false,std::size_t externalCount=0);
    // 仅未收敛的弹簧需要定时器；静止悬停、拖动和外部提示均会停止。
    bool Active()const;
    void Reset();
private:
    struct Spring { float value=0,velocity=0; };
    struct ItemMotion {
        Spring x,y,width,height,scale{1,0},lift;
        bool dragging=false,floating=false;
    };
    static bool Step(Spring&,float target,float dt,bool reducedMotion,float epsilon);
    std::unordered_map<std::string,ItemMotion> items_;
    std::unordered_set<std::string> knownIds_;
    std::string collectionId_;
    std::optional<OrganizerPoint> externalPointer_;
    OrganizerMotionFrame frame_;
    Spring feedback_;
    bool active_=false;
};
}
