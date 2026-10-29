#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace lume::screenshot {
struct Point { int x=0,y=0; };
struct Rect {
    int left=0,top=0,right=0,bottom=0;
    int Width()const{return right-left;} int Height()const{return bottom-top;}
    bool Contains(Point p)const{return p.x>=left&&p.x<right&&p.y>=top&&p.y<bottom;}
    bool Valid()const{return Width()>1&&Height()>1;}
    bool operator==(const Rect& r)const{return left==r.left&&top==r.top&&right==r.right&&bottom==r.bottom;}
};
enum Handle { None=0,Left=1,Top=2,Right=4,Bottom=8,Move=16 };
enum class Tool { Select,Shape,Arrow,Pen,Marker,Mosaic,Text,Eraser };
struct Annotation {
    Tool tool=Tool::Pen;
    std::vector<Point> points;
    std::wstring text;
    std::uint32_t color=0xef4444;
    int width=3,variant=0;
    std::wstring fontFamily=L"Microsoft YaHei UI";
    int fontSize=0,brushSize=0;
    bool bold=false,italic=false,outline=false;
};
inline Rect Normalize(Point a,Point b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};}
inline Point Clamp(Point p,Rect b){return {std::clamp(p.x,b.left,b.right),std::clamp(p.y,b.top,b.bottom)};}
inline Rect Constrain(Rect r,Rect b){return Normalize(Clamp({r.left,r.top},b),Clamp({r.right,r.bottom},b));}
inline int HitHandle(Rect r,Point p,int tolerance){
    const Point points[]{{r.left,r.top},{(r.left+r.right)/2,r.top},{r.right,r.top},
        {r.right,(r.top+r.bottom)/2},{r.right,r.bottom},{(r.left+r.right)/2,r.bottom},
        {r.left,r.bottom},{r.left,(r.top+r.bottom)/2}};
    constexpr int hits[]{Left|Top,Top,Right|Top,Right,Right|Bottom,Bottom,Left|Bottom,Left};
    for(int i=0;i<8;++i)if(std::abs(p.x-points[i].x)<=tolerance&&std::abs(p.y-points[i].y)<=tolerance)return hits[i];
    return r.Contains(p)?Move:None;
}
inline Rect Drag(Rect r,int handle,Point delta,Rect bounds){
    if(handle==Move){
        delta.x=std::clamp(delta.x,bounds.left-r.left,bounds.right-r.right);
        delta.y=std::clamp(delta.y,bounds.top-r.top,bounds.bottom-r.bottom);
        return {r.left+delta.x,r.top+delta.y,r.right+delta.x,r.bottom+delta.y};
    }
    if(handle&Left)r.left+=delta.x;
    if(handle&Right)r.right+=delta.x;
    if(handle&Top)r.top+=delta.y;
    if(handle&Bottom)r.bottom+=delta.y;
    return Constrain(r,bounds);
}
inline Rect ToolbarBounds(Rect selection,Rect bounds,int width,int height,int gap){
    width=std::min(width,bounds.Width());height=std::min(height,bounds.Height());
    const int x=std::clamp(selection.right-width,bounds.left,bounds.right-width);
    int y=selection.bottom+gap;
    if(y+height>bounds.bottom)y=selection.top-gap-height;
    y=std::clamp(y,bounds.top,bounds.bottom-height);
    return {x,y,x+width,y+height};
}
inline double SegmentDistance(Point p,Point a,Point b){
    const double dx=static_cast<double>(b.x)-a.x,dy=static_cast<double>(b.y)-a.y,n=dx*dx+dy*dy;
    const double px=static_cast<double>(p.x)-a.x,py=static_cast<double>(p.y)-a.y;
    const double t=n?std::clamp((px*dx+py*dy)/n,0.,1.):0.;
    return std::hypot(px-t*dx,py-t*dy);
}
inline int MosaicBrushRadius(int width){return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(width)*5,8,std::numeric_limits<int>::max()));}
inline int AnnotationBrushRadius(const Annotation& a){return a.brushSize>0?std::max(1,a.brushSize/2):MosaicBrushRadius(a.width);}
inline double AnnotationFontSize(const Annotation& a){return a.fontSize>0?a.fontSize:std::max(1.,static_cast<double>(a.width)+18);}
inline bool Hit(const Annotation& a,Point p,int radius){
    if(a.points.empty())return false;
    const auto first=a.points.front(),last=a.points.back();const auto r=Normalize(first,last);
    const double tolerance=radius+(a.tool==Tool::Mosaic&&a.variant>=2?static_cast<double>(AnnotationBrushRadius(a)):a.width*(a.tool==Tool::Marker?2.5:.5));
    if(a.tool==Tool::Text){
        int lines=1,longest=0,current=0;
        if(a.text.empty())return false;
        for(auto c:a.text){if(c==L'\n'){longest=std::max(longest,current);current=0;++lines;}else if(c==L'\t')current+=8;else if(c!=L'\r')++current;}
        longest=std::max(longest,current);
        // Core has no font backend: use a conservative em envelope for any family,
        // including italic overhang and outline ink. Rendering measures actual glyphs.
        const double font=AnnotationFontSize(a),margin=radius+3+font*(a.italic?.5:.25)+(a.outline?std::max(1.,font/24):0);
        const double x=static_cast<double>(p.x)-first.x,y=static_cast<double>(p.y)-first.y;
        return x>=-margin&&y>=-margin&&x<std::max(font,longest*font*(a.bold?1.2:1.1))+margin&&y<lines*font*1.8+margin;
    }
    if(a.tool==Tool::Mosaic&&a.variant<2)return r.Contains(p);
    if(a.tool==Tool::Arrow){
        const double dx=static_cast<double>(last.x)-first.x,dy=static_cast<double>(last.y)-first.y,distance=std::hypot(dx,dy);
        const double ux=distance?dx/distance:1,uy=distance?dy/distance:0;
        const double px=static_cast<double>(p.x)-first.x,py=static_cast<double>(p.y)-first.y;
        const double along=px*ux+py*uy,across=-px*uy+py*ux,length=std::max(12.,static_cast<double>(a.width)*4);
        struct Vertex {double x,y;};
        const auto polygonHit=[&](std::initializer_list<Vertex> polygon){
            bool inside=false;auto previous=*(polygon.end()-1);
            for(auto v:polygon){
                const double ex=v.x-previous.x,ey=v.y-previous.y,n=ex*ex+ey*ey;
                const double t=n?std::clamp(((along-previous.x)*ex+(across-previous.y)*ey)/n,0.,1.):0;
                if(std::hypot(along-previous.x-t*ex,across-previous.y-t*ey)<=radius)return true;
                if((v.y>across)!=(previous.y>across)&&along<(previous.x-v.x)*(across-v.y)/(previous.y-v.y)+v.x)inside=!inside;
                previous=v;
            }
            return inside;
        };
        if(a.variant==3&&distance>0){
            const double head=std::min(length,distance*.6),shoulder=distance-head,neck=std::min(std::max(1.,static_cast<double>(a.width))*.9,head*.35);
            return polygonHit({{0,0},{shoulder,neck},{shoulder,head*.5},{distance,0},{shoulder,-head*.5},{shoulder,-neck}});
        }
        if(SegmentDistance(p,first,last)<=tolerance)return true;
        const double inset=length*std::cos(.45),half=length*std::sin(.45);
        if((a.variant==0||a.variant==2)&&polygonHit({{distance,0},{distance-inset,half},{distance-inset,-half}}))return true;
        return a.variant==2&&polygonHit({{0,0},{inset,half},{inset,-half}});
    }
    if(a.tool==Tool::Shape){
        if(a.variant==2)return r.Contains(p);
        if(a.variant==3){const double rx=std::max(1.,r.Width()*.5),ry=std::max(1.,r.Height()*.5);return std::hypot((p.x-r.left-rx)/rx,(p.y-r.top-ry)/ry)<=1;}
        if(a.variant==1){const double rx=std::max(1.,r.Width()*.5),ry=std::max(1.,r.Height()*.5);
            return std::abs(std::hypot((p.x-r.left-rx)/rx,(p.y-r.top-ry)/ry)-1)*std::min(rx,ry)<=tolerance;}
        return SegmentDistance(p,{r.left,r.top},{r.right,r.top})<=tolerance||SegmentDistance(p,{r.right,r.top},{r.right,r.bottom})<=tolerance||
            SegmentDistance(p,{r.right,r.bottom},{r.left,r.bottom})<=tolerance||SegmentDistance(p,{r.left,r.bottom},{r.left,r.top})<=tolerance;
    }
    for(std::size_t i=1;i<a.points.size();++i)if(SegmentDistance(p,a.points[i-1],a.points[i])<=tolerance)return true;
    return SegmentDistance(p,first,first)<=tolerance;
}
class History {
    std::vector<std::vector<Annotation>> states_{{}};std::size_t index_=0;
    static std::size_t Bytes(const std::vector<Annotation>& items){
        std::size_t bytes=sizeof(items);
        for(const auto& a:items)bytes+=sizeof(a)+a.points.size()*sizeof(Point)+(a.text.size()+a.fontFamily.size())*sizeof(wchar_t);
        return bytes;
    }
    void Commit(std::vector<Annotation> next){
        states_.resize(index_+1);states_.push_back(std::move(next));
        // Bound deep snapshots as well as operation count for long pen strokes.
        std::size_t bytes=0;for(const auto& state:states_)bytes+=Bytes(state);
        while(states_.size()>1&&(states_.size()>65||bytes>32u*1024u*1024u)){
            bytes-=Bytes(states_.front());states_.erase(states_.begin());
        }
        index_=states_.size()-1;
    }
public:
    const std::vector<Annotation>& Items()const{return states_[index_];}
    bool Add(Annotation a){
        if(a.points.empty()||a.points.size()>20000||Items().size()>=256||a.text.size()>4096)return false;
        if(a.fontSize<0||a.fontSize>200||a.fontFamily.size()>128)return false;
        if(a.tool==Tool::Text&&a.text.empty())return false;
        if(Bytes(Items())+sizeof(a)+a.points.size()*sizeof(Point)+(a.text.size()+a.fontFamily.size())*sizeof(wchar_t)>16u*1024u*1024u)return false;
        auto next=Items();next.push_back(std::move(a));Commit(std::move(next));return true;
    }
    bool UpdateLast(Annotation a){
        if(Items().empty()||a.points.empty()||a.points.size()>20000||a.text.size()>4096)return false;
        if(a.fontSize<0||a.fontSize>200||a.fontFamily.size()>128)return false;
        if(a.tool==Tool::Text&&a.text.empty())return false;
        states_[index_].back()=std::move(a);return true;
    }
    bool CanUndo()const{return index_>0;}bool CanRedo()const{return index_+1<states_.size();}
    bool Undo(){if(!CanUndo())return false;--index_;return true;}
    bool Redo(){if(!CanRedo())return false;++index_;return true;}
    void Clear(){states_={{}};index_=0;}
};
}
