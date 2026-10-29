#include "platform/windows/screenshot/screenshot_render.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/icon_art.h"
#include <cmath>
#include <array>
#include <memory>
#include <limits>

namespace lume::platform::windows::capture {
namespace {
using namespace Gdiplus;
Color Ink(std::uint32_t rgb,BYTE alpha=255){return Color(alpha,static_cast<BYTE>(rgb>>16),static_cast<BYTE>(rgb>>8),static_cast<BYTE>(rgb));}
using Bounds=screenshot::Rect;
bool Nonempty(Bounds r){return r.right>r.left&&r.bottom>r.top;}
Bounds Intersect(Bounds a,Bounds b){
    Bounds r{std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
    return Nonempty(r)?r:Bounds{};
}
Bounds Expand(Bounds r,double amount,Bounds clip){
    if(!Nonempty(clip))return {};
    const auto coordinate=[](double value){return static_cast<int>(std::clamp(value,static_cast<double>(std::numeric_limits<int>::min()),static_cast<double>(std::numeric_limits<int>::max())));};
    return Intersect({coordinate(std::floor(r.left-amount)),coordinate(std::floor(r.top-amount)),
        coordinate(std::ceil(r.right+amount)),coordinate(std::ceil(r.bottom+amount))},clip);
}
int BlockSize(int width){return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(width)*3,6,std::numeric_limits<int>::max()));}
int BlurRadius(int width){return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(width)*2,3,40));}
Bounds BrushBounds(const screenshot::Annotation& a,Bounds clip){
    auto p=a.points.front();Bounds r{p.x,p.y,p.x,p.y};
    for(auto point:a.points){r.left=std::min(r.left,point.x);r.top=std::min(r.top,point.y);r.right=std::max(r.right,point.x);r.bottom=std::max(r.bottom,point.y);}
    return Expand(r,static_cast<double>(screenshot::AnnotationBrushRadius(a))+1,clip);
}
Bounds GridBounds(Bounds r,Bounds clip,int block){
    if(!Nonempty(r))return {};
    const auto low=[&](int value,int origin){return static_cast<int>(origin+(static_cast<std::int64_t>(value)-origin)/block*block);};
    const auto high=[&](int value,int origin,int end){return static_cast<int>(std::min<std::int64_t>(end,origin+((static_cast<std::int64_t>(value)-origin+block-1)/block)*block));};
    return {low(r.left,clip.left),low(r.top,clip.top),high(r.right,clip.left,clip.right),high(r.bottom,clip.top,clip.bottom)};
}
// Rasterize each capsule by scanline, then union into a byte mask. Work scales with
// segment scanlines and covered spans, never with bounding area times segment count.
std::vector<BYTE> BrushMask(const screenshot::Annotation& a,Bounds rect){
    std::vector<BYTE> mask(static_cast<std::size_t>(rect.Width())*rect.Height());
    const double radius=screenshot::AnnotationBrushRadius(a);
    const auto capsule=[&](screenshot::Point p,screenshot::Point q){
        const double dx=static_cast<double>(q.x)-p.x,dy=static_cast<double>(q.y)-p.y,length=std::hypot(dx,dy);
        const double nx=length?-dy/length*radius:0,ny=length?dx/length*radius:0;
        const double xs[]{p.x+nx,q.x+nx,q.x-nx,p.x-nx},ys[]{p.y+ny,q.y+ny,q.y-ny,p.y-ny};
        const double clippedTop=std::max<double>(rect.top,std::ceil(std::min(p.y,q.y)-radius));
        const double clippedBottom=std::min<double>(rect.bottom-1,std::floor(std::max(p.y,q.y)+radius));
        if(clippedTop>clippedBottom)return;
        const int top=static_cast<int>(clippedTop),bottom=static_cast<int>(clippedBottom);
        for(int y=top;y<=bottom;++y){
            double left=std::numeric_limits<double>::infinity(),right=-left;
            const auto include=[&](double x){left=std::min(left,x);right=std::max(right,x);};
            for(auto center:{p,q}){
                const double delta=static_cast<double>(y)-center.y;
                if(std::abs(delta)<=radius){const double half=std::sqrt(std::max(0.,radius*radius-delta*delta));include(center.x-half);include(center.x+half);}
            }
            if(length)for(int i=0;i<4;++i){const int j=(i+1)%4;
                if(y<std::min(ys[i],ys[j])||y>std::max(ys[i],ys[j]))continue;
                if(ys[i]==ys[j]){include(xs[i]);include(xs[j]);}
                else include(xs[i]+(y-ys[i])*(xs[j]-xs[i])/(ys[j]-ys[i]));
            }
            if(left>right||left>rect.right-1||right<rect.left)continue;
            const int start=static_cast<int>(std::max<double>(rect.left,std::ceil(left-1e-7)));
            const int end=static_cast<int>(std::min<double>(rect.right-1,std::floor(right+1e-7)))+1;
            auto row=mask.begin()+static_cast<std::size_t>(y-rect.top)*rect.Width();
            std::fill(row+(start-rect.left),row+(end-rect.left),BYTE{1});
        }
    };
    if(a.points.size()==1)capsule(a.points.front(),a.points.front());
    else for(std::size_t i=1;i<a.points.size();++i)capsule(a.points[i-1],a.points[i]);
    return mask;
}
void Filter(Image& image,Bounds rect,bool blur,int width,Bounds samples,const std::vector<BYTE>* mask=nullptr){
    if(!Nonempty(rect))return;
    const auto covered=[&](int x,int y){return rect.Contains({x,y})&&(!mask||(*mask)[static_cast<std::size_t>(y-rect.top)*rect.Width()+x-rect.left]!=0);};
    const int block=BlockSize(width);
    if(!blur){
        const auto grid=mask?GridBounds(rect,samples,block):rect;
        for(int y=grid.top;y<grid.bottom;) {
          const int endY=static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(y)+block,grid.bottom));
          for(int x=grid.left;x<grid.right;){
            const int endX=static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(x)+block,grid.right));
            bool needed=!mask;
            for(int py=std::max(y,rect.top);!needed&&py<std::min(endY,rect.bottom);++py)
                for(int px=std::max(x,rect.left);px<std::min(endX,rect.right);++px)if(covered(px,py)){needed=true;break;}
            if(!needed){x=endX;continue;}
            std::uint64_t red=0,green=0,blue=0,count=0;
            for(int py=y;py<endY;++py)for(int px=x;px<endX;++px){const auto p=image.pixels[static_cast<std::size_t>(py)*image.width+px];red+=(p>>16)&255;green+=(p>>8)&255;blue+=p&255;++count;}
            const auto color=0xff000000u|static_cast<unsigned>((red/count)<<16)|static_cast<unsigned>((green/count)<<8)|static_cast<unsigned>(blue/count);
            for(int py=std::max(y,rect.top);py<std::min(endY,rect.bottom);++py)for(int px=std::max(x,rect.left);px<std::min(endX,rect.right);++px)
                if(covered(px,py))image.pixels[static_cast<std::size_t>(py)*image.width+px]=color;
            x=endX;
          }
          y=endY;
        }
        return;
    }
    // Cache only the horizontal rows still needed by the vertical window. Future
    // source rows are read before any writes reach them, so no source copy is needed.
    const int radius=BlurRadius(width),w=rect.Width(),rows=radius*2+1;
    std::vector<std::uint32_t> horizontal(static_cast<std::size_t>(w)*rows);
    struct Sum {std::uint64_t r=0,g=0,b=0;};
    std::vector<Sum> sums(static_cast<std::size_t>(w));
    const auto horizontalRow=[&](int y){
        std::uint64_t r=0,g=0,b=0;
        int low=std::max(samples.left,rect.left-radius),high=low-1;
        for(int x=0;x<w;++x){
            const int start=std::max(samples.left,rect.left+x-radius),end=static_cast<int>(std::min<std::int64_t>(samples.right-1,static_cast<std::int64_t>(rect.left)+x+radius));
            while(high<end){const auto p=image.pixels[static_cast<std::size_t>(y)*image.width+(++high)];r+=(p>>16)&255;g+=(p>>8)&255;b+=p&255;}
            while(low<start){const auto p=image.pixels[static_cast<std::size_t>(y)*image.width+(low++)];r-=(p>>16)&255;g-=(p>>8)&255;b-=p&255;}
            const auto count=static_cast<unsigned>(high-low+1);
            const auto color=0xff000000u|static_cast<unsigned>((r/count)<<16)|static_cast<unsigned>((g/count)<<8)|static_cast<unsigned>(b/count);
            horizontal[static_cast<std::size_t>(y%rows)*w+x]=color;
            sums[x].r+=(color>>16)&255;sums[x].g+=(color>>8)&255;sums[x].b+=color&255;
        }
    };
    int low=std::max(samples.top,rect.top-radius),high=low-1;
    for(int y=rect.top;y<rect.bottom;++y){
        const int start=std::max(samples.top,y-radius),end=static_cast<int>(std::min<std::int64_t>(samples.bottom-1,static_cast<std::int64_t>(y)+radius));
        while(low<start){
            for(int x=0;x<w;++x){const auto p=horizontal[static_cast<std::size_t>(low%rows)*w+x];sums[x].r-=(p>>16)&255;sums[x].g-=(p>>8)&255;sums[x].b-=p&255;}
            ++low;
        }
        while(high<end)horizontalRow(++high);
        const auto count=static_cast<unsigned>(high-low+1);
        for(int x=0;x<w;++x)if(covered(rect.left+x,y)){
            const auto& s=sums[x];image.pixels[static_cast<std::size_t>(y)*image.width+rect.left+x]=0xff000000u|static_cast<unsigned>((s.r/count)<<16)|static_cast<unsigned>((s.g/count)<<8)|static_cast<unsigned>(s.b/count);
        }
    }
}
direct_write::Style TextStyle(const screenshot::Annotation& a){
    direct_write::Style style;style.size=static_cast<float>(screenshot::AnnotationFontSize(a));
    style.family=a.fontFamily.empty()?L"Microsoft YaHei UI":a.fontFamily.c_str();
    style.weight=a.bold?FW_BOLD:FW_NORMAL;style.italic=a.italic;style.flags=DT_NOPREFIX;return style;
}
void DrawAnnotationText(Graphics& g,const screenshot::Annotation& a){
    const auto style=TextStyle(a);const auto measured=direct_write::Measure(a.text,0,style);
    if(measured.Height<=0)return;
    RectF clip;g.GetClipBounds(&clip);const auto first=a.points.front();
    const float width=std::min(measured.Width+style.size,std::max(0.f,clip.GetRight()-first.x));
    const float height=std::min(measured.Height+style.size,std::max(0.f,clip.GetBottom()-first.y));
    if(width<=0||height<=0)return;
    const RectF rect(static_cast<float>(first.x),static_cast<float>(first.y),width,height);
    if(!a.outline){direct_write::Draw(g,a.text,rect,Ink(a.color),style);return;}
    // Derive a hollow stroke from the DirectWrite glyph coverage. Both modes
    // share the same shaping, fallback, line spacing and glyph positions.
    const int w=static_cast<int>(std::ceil(width)),h=static_cast<int>(std::ceil(height));
    std::vector<std::uint32_t> mask(static_cast<std::size_t>(w)*h),stroke(mask.size());
    Bitmap bitmap(w,h,w*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(mask.data()));
    {Graphics text(&bitmap);if(!direct_write::Draw(text,a.text,{0,0,width,height},Color(255,255,255,255),style))return;}
    const float radius=std::max(.5f,style.size/48.f);
    std::array<PointF,8> offsets{};
    for(std::size_t i=0;i<offsets.size();++i){const double angle=i*3.14159265358979323846/4;
        offsets[i]={radius*static_cast<float>(std::cos(angle)),radius*static_cast<float>(std::sin(angle))};}
    const auto sample=[&](float x,float y){
        const int left=static_cast<int>(std::floor(x)),top=static_cast<int>(std::floor(y));const float fx=x-left,fy=y-top;
        const auto at=[&](int px,int py){return px>=0&&py>=0&&px<w&&py<h?static_cast<float>(mask[static_cast<std::size_t>(py)*w+px]>>24):0.f;};
        return at(left,top)*(1-fx)*(1-fy)+at(left+1,top)*fx*(1-fy)+at(left,top+1)*(1-fx)*fy+at(left+1,top+1)*fx*fy;
    };
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        float low=sample(static_cast<float>(x),static_cast<float>(y)),high=low;
        for(const auto offset:offsets){const float alpha=sample(x+offset.X,y+offset.Y);low=std::min(low,alpha);high=std::max(high,alpha);}
        const auto alpha=static_cast<std::uint32_t>(std::clamp(std::lround(high-low),0L,255L));
        stroke[static_cast<std::size_t>(y)*w+x]=(alpha<<24)|((((a.color>>16)&255)*alpha/255)<<16)|((((a.color>>8)&255)*alpha/255)<<8)|((a.color&255)*alpha/255);
    }
    Bitmap outline(w,h,w*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(stroke.data()));
    g.DrawImage(&outline,rect,0,0,width,height,UnitPixel);
}
void Annotation(Graphics& g,const screenshot::Annotation& a){
    using screenshot::Tool;if(a.points.empty())return;
    const auto first=a.points.front(),last=a.points.back();const auto r=screenshot::Normalize(first,last);
    const auto rect=RectF(static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.Width()),static_cast<float>(r.Height()));
    const bool marker=a.tool==Tool::Marker;
    Pen pen(Ink(a.color,marker?85:255),static_cast<float>(marker?a.width*5:a.width));
    pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);pen.SetLineJoin(LineJoinRound);
    SolidBrush brush(Ink(a.color));
    if(a.tool==Tool::Shape){
        if(a.variant==1)g.DrawEllipse(&pen,rect);else if(a.variant==2)g.FillRectangle(&brush,rect);
        else if(a.variant==3)g.FillEllipse(&brush,rect);else g.DrawRectangle(&pen,rect);
    }else if(a.tool==Tool::Text){
        DrawAnnotationText(g,a);
    }else if(a.tool==Tool::Arrow){
        const double dx=static_cast<double>(last.x)-first.x,dy=static_cast<double>(last.y)-first.y,distance=std::hypot(dx,dy);
        const double angle=std::atan2(dy,dx),length=std::max(12.,static_cast<double>(a.width)*4);
        if(a.variant==3&&distance>0){
            const double ux=dx/distance,uy=dy/distance,head=std::min(length,distance*.6),neck=std::min(std::max(1.,static_cast<double>(a.width))*.9,head*.35);
            const auto point=[&](double along,double across){return PointF(static_cast<REAL>(first.x+ux*along-uy*across),static_cast<REAL>(first.y+uy*along+ux*across));};
            const double shoulder=distance-head;
            PointF polygon[]{point(0,0),point(shoulder,neck),point(shoulder,head*.5),point(distance,0),point(shoulder,-head*.5),point(shoulder,-neck)};
            g.FillPolygon(&brush,polygon,6);
        }else{
            const double inset=(a.variant==0||a.variant==2)?length*std::cos(.45):0;
            const auto bodyPoint=[&](screenshot::Point p,double direction,double distance){return PointF(
                static_cast<REAL>(p.x-distance*std::cos(direction)),static_cast<REAL>(p.y-distance*std::sin(direction)));};
            const PointF bodyStart=(a.variant==2)?bodyPoint(first,angle+3.14159265358979323846,inset):PointF(static_cast<REAL>(first.x),static_cast<REAL>(first.y));
            const PointF bodyEnd=(a.variant==0||a.variant==2)?bodyPoint(last,angle,inset):PointF(static_cast<REAL>(last.x),static_cast<REAL>(last.y));
            g.DrawLine(&pen,bodyStart,bodyEnd);
            const auto head=[&](screenshot::Point end,double direction){
                PointF tip[]{PointF(static_cast<REAL>(end.x),static_cast<REAL>(end.y)),
                    PointF(static_cast<REAL>(end.x-length*std::cos(direction-.45)),static_cast<REAL>(end.y-length*std::sin(direction-.45))),
                    PointF(static_cast<REAL>(end.x-length*std::cos(direction+.45)),static_cast<REAL>(end.y-length*std::sin(direction+.45)))};
                g.FillPolygon(&brush,tip,3);
            };
            if(a.variant==0||a.variant==2)head(last,angle);
            if(a.variant==2)head(first,angle+3.14159265358979323846);
        }
    }else if(a.tool==Tool::Pen||marker){
        if(a.points.size()==1){SolidBrush dot(Ink(a.color,marker?85:255));const float width=pen.GetWidth();g.FillEllipse(&dot,first.x-width/2,first.y-width/2,width,width);return;}
        std::vector<PointF> points;points.reserve(a.points.size());
        for(std::size_t i=0;i<a.points.size();++i){
            const auto p=a.points[i];
            if(i==0||i+1==a.points.size())points.emplace_back(static_cast<float>(p.x),static_cast<float>(p.y));
            else {
                const auto before=a.points[i-1],after=a.points[i+1];
                points.emplace_back((before.x+2.f*p.x+after.x)*.25f,(before.y+2.f*p.y+after.y)*.25f);
            }
        }
        GraphicsPath path;path.AddCurve(points.data(),static_cast<INT>(points.size()),.5f);g.DrawPath(&pen,&path);
    }
}
}
screenshot::Rect AnnotationBounds(const screenshot::Annotation& a,screenshot::Rect clip){
    using screenshot::Tool;
    if(a.points.empty()||!Nonempty(clip))return {};
    auto first=a.points.front();auto r=screenshot::Normalize(first,a.points.back());
    if(a.tool==Tool::Eraser)return BrushBounds(a,clip);
    if(a.tool==Tool::Mosaic){
        if(a.variant<2)return Intersect(r,clip);
        r=BrushBounds(a,clip);if(!Nonempty(r))return {};
        // Bounds cover reads too: the preview may have UI pixels outside the restored region.
        return a.variant==3?Expand(r,BlurRadius(a.width),clip):GridBounds(r,clip,BlockSize(a.width));
    }
    if(a.tool==Tool::Text){
        if(a.text.empty())return {};
        const double size=screenshot::AnnotationFontSize(a);
        auto measured=direct_write::Measure(a.text,0,TextStyle(a));if(measured.Height<=0)return clip;
        measured.X=static_cast<float>(first.x);measured.Y=static_cast<float>(first.y);
        const double margin=std::max(3.,size*.5);
        const double left=std::max<double>(clip.left,std::floor(measured.X-margin)),top=std::max<double>(clip.top,std::floor(measured.Y-margin));
        const double right=std::min<double>(clip.right,std::ceil(measured.X+measured.Width+margin)),bottom=std::min<double>(clip.bottom,std::ceil(measured.Y+measured.Height+margin));
        if(left>=right||top>=bottom)return {};
        return {static_cast<int>(left),static_cast<int>(top),static_cast<int>(right),static_cast<int>(bottom)};
    }
    if(a.tool!=Tool::Shape&&a.tool!=Tool::Arrow&&a.tool!=Tool::Pen&&a.tool!=Tool::Marker)return {};
    if(a.tool==Tool::Pen||a.tool==Tool::Marker)for(auto point:a.points){r.left=std::min(r.left,point.x);r.top=std::min(r.top,point.y);r.right=std::max(r.right,point.x);r.bottom=std::max(r.bottom,point.y);}
    double margin=std::max(1.,static_cast<double>(a.width))*(a.tool==Tool::Marker?2.5:.5)+3;
    if(a.tool==Tool::Arrow&&a.variant!=1)margin=std::max(margin,std::max(12.,static_cast<double>(a.width)*4)+3);
    return Expand(r,margin,clip);
}
void RenderAnnotation(Image& image,const screenshot::Annotation& a,screenshot::Rect clip){
    if(!image.Valid()||a.points.empty()||a.tool==screenshot::Tool::Eraser)return;
    clip=Intersect(clip,{0,0,image.width,image.height});if(!Nonempty(clip))return;
    if(a.tool==screenshot::Tool::Mosaic){
        if(a.variant<2){const auto r=Intersect(screenshot::Normalize(a.points.front(),a.points.back()),clip);Filter(image,r,a.variant==1,a.width,r);}
        else {const auto r=BrushBounds(a,clip);if(!Nonempty(r))return;const auto mask=BrushMask(a,r);Filter(image,r,a.variant==3,a.width,clip,&mask);}
        return;
    }
    Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(image.pixels.data()));
    Graphics g(&bitmap);g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetClip(Rect(clip.left,clip.top,clip.Width(),clip.Height()));Annotation(g,a);
}
void RenderAnnotation(Image& image,const Image& original,const screenshot::Annotation& a,screenshot::Rect clip){
    if(a.tool!=screenshot::Tool::Eraser){RenderAnnotation(image,a,clip);return;}
    if(!image.Valid()||!original.Valid()||image.width!=original.width||image.height!=original.height||a.points.empty())return;
    clip=Intersect(clip,{0,0,image.width,image.height});if(!Nonempty(clip))return;
    const auto bounds=BrushBounds(a,clip);if(!Nonempty(bounds))return;
    const auto mask=BrushMask(a,bounds);
    for(int y=bounds.top;y<bounds.bottom;++y)for(int x=bounds.left;x<bounds.right;++x){
        const auto i=static_cast<std::size_t>(y-bounds.top)*bounds.Width()+x-bounds.left;
        if(mask[i]){const auto pixel=static_cast<std::size_t>(y)*image.width+x;image.pixels[pixel]=original.pixels[pixel];}
    }
}
Image Composite(const Image& source,const std::vector<screenshot::Annotation>& annotations,screenshot::Rect clip){
    if(!source.Valid())return {};
    Image result=source;
    for(const auto& a:annotations)RenderAnnotation(result,source,a,clip);
    return result;
}
const wchar_t* CommandName(Command command){
    constexpr const wchar_t* names[]{L"\u5f62\u72b6",L"\u7bad\u5934 / \u76f4\u7ebf",L"\u753b\u7b14",L"\u8367\u5149\u7b14",L"\u9a6c\u8d5b\u514b / \u6a21\u7cca",L"\u6587\u5b57",L"\u6a61\u76ae\u64e6",L"\u64a4\u9500",L"\u91cd\u505a",L"\u5173\u95ed",L"\u8d34\u56fe",L"\u4fdd\u5b58",L"\u590d\u5236"};
    const int i=static_cast<int>(command);return i>=0&&i<CommandCount?names[i]:L"";
}
void DrawIcon(Graphics& g,Command command,float x,float y,float size,Color color){
    const int index=static_cast<int>(command);
    if(index<0||index>=CommandCount)return;
    icon_art::Draw(g,static_cast<icon_art::Icon>(index),x,y,size,color);
}
}
