#pragma once
#include "core/almanac/almanac.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/material/material_api.h"
#include <array>
namespace lume::platform::windows {
class AlmanacView {
public:
    void SetDate(calendar::Date);
    void Draw(WidgetCanvas&,float width,float height);
    std::vector<LumeMaterialPanel> Panels(float width,float height)const;
    int ActivityAt(float x,float y)const;
    bool Scroll(int row,float delta,bool absolute=false);
    void Focus(int row){focused_=row;}
    calendar::Date Date()const{return date_;}
    bool Available()const{return day_.has_value();}
    float Offset(int row)const{return row>=0&&row<2?offsets_[row]:0;}
    unsigned Revision()const{return revision_;}
private:
    calendar::Date date_{};
    std::optional<almanac::Day> day_;
    std::wstring lunar_,year_,cycle_;
    std::array<std::wstring,2> activities_;
    std::array<WidgetRect,2> rows_{},viewports_{};
    std::array<float,2> offsets_{},maximum_{};
    int focused_=-1;
    unsigned revision_=0;
};
}
