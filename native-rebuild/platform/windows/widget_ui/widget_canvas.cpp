#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/widget_ui/ui_font.h"
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
namespace lume::platform::windows {
namespace {
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){Reset();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T* operator->()const{return value;}
    void Reset(){if(value)value->Release();value=nullptr;}
    T** Put(){Reset();return &value;}
};
struct Factories {
    Com<ID2D1Factory> draw;
    Com<IDWriteFactory> write;
    Com<IWICImagingFactory> imaging;
    bool Initialize(){
        return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,draw.Put()))&&
            SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.Put())))&&
            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(imaging.Put())));
    }
};
// These objects have apartment/thread affinity; no process-wide strong owner.
std::shared_ptr<Factories> AcquireFactories(){
    thread_local std::weak_ptr<Factories> shared;
    auto result=shared.lock();
    if(!result){result=std::make_shared<Factories>();if(!result->Initialize())return {};shared=result;}
    return result;
}
D2D1_RECT_F Rect(WidgetRect r){return D2D1::RectF(r.x,r.y,r.x+r.width,r.y+r.height);}
}
struct WidgetCanvas::State {
    std::shared_ptr<Factories> factories;
    Com<IWICBitmap> bitmap;
    Com<ID2D1RenderTarget> target;
    Com<ID2D1SolidColorBrush> brush;
    using Key=std::tuple<float,WidgetTextWeight,WidgetTextAlign,bool>;
    std::map<Key,std::unique_ptr<Com<IDWriteTextFormat>>> formats;
    struct ResourceVariant {
        unsigned width=0,height=0;
        std::unique_ptr<Com<ID2D1Bitmap>> bitmap;
    };
    struct ResourceImage {
        unsigned width=0,height=0;
        std::vector<ResourceVariant> variants;
    };
    std::map<unsigned,ResourceImage> images;
    struct Image {
        std::shared_ptr<const WidgetBitmap> pixels;
        std::unique_ptr<Com<ID2D1Bitmap>> bitmap;
        bool used = false;
    };
    std::vector<Image> preparedImages;
    std::vector<Image> frameImages;
    unsigned width=0,height=0,dpi=0;
    bool drawing=false;
    unsigned clips=0;
    std::uint64_t generation=0;
    ~State(){if(drawing){while(clips){target->PopAxisAlignedClip();--clips;}target->EndDraw();}}
    void Color(std::uint32_t rgb,float alpha){brush->SetColor(D2D1::ColorF(rgb,std::clamp(alpha,0.f,1.f)));}
    IDWriteTextFormat* Format(float size,WidgetTextWeight weight,WidgetTextAlign align,std::wstring_view text){
        const Key key{size,weight,align,ui_font::IsChinese(text)};
        auto found=formats.find(key);
        if(found==formats.end()){
            if(formats.size()>=48)formats.clear();
            auto format=std::make_unique<Com<IDWriteTextFormat>>();
            if(FAILED(factories->write->CreateTextFormat(ui_font::Family(text),nullptr,static_cast<DWRITE_FONT_WEIGHT>(weight),
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",format->Put())))return nullptr;
            (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            (*format)->SetTextAlignment(align==WidgetTextAlign::Center?DWRITE_TEXT_ALIGNMENT_CENTER:align==WidgetTextAlign::Right?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
            found=formats.emplace(key,std::move(format)).first;
        }
        return found->second->value;
    }
};
WidgetCanvas::WidgetCanvas():state_(std::make_unique<State>()){}
WidgetCanvas::~WidgetCanvas()=default;
bool WidgetCanvas::Begin(float width,float height,unsigned dpi){
    auto& s=*state_;
    if(s.drawing||!dpi||dpi>768||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
    const double pixelWidth=std::round(static_cast<double>(width)*dpi/96),pixelHeight=std::round(static_cast<double>(height)*dpi/96);
    if(pixelWidth<1||pixelHeight<1||pixelWidth>8192||pixelHeight>8192||pixelWidth*pixelHeight>16777216)return false;
    if(!s.factories){s.factories=AcquireFactories();if(!s.factories)return false;}
    const auto w=static_cast<unsigned>(pixelWidth),h=static_cast<unsigned>(pixelHeight);
    if(!s.target.value||s.width!=w||s.height!=h||s.dpi!=dpi){
        s.images.clear();s.preparedImages.clear();s.frameImages.clear();s.brush.Reset();s.target.Reset();s.bitmap.Reset();
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
        if(FAILED(s.factories->imaging->CreateBitmap(w,h,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,s.bitmap.Put()))||
           FAILED(s.factories->draw->CreateWicBitmapRenderTarget(s.bitmap.value,properties,s.target.Put()))||
           FAILED(s.target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),s.brush.Put()))){
            s.brush.Reset();s.target.Reset();s.bitmap.Reset();return false;
        }
        s.width=w;s.height=h;s.dpi=dpi;++s.generation;
    }
    s.target->BeginDraw();s.target->SetTransform(D2D1::Matrix3x2F::Identity());s.target->Clear(D2D1::ColorF(0,0,0,0));
    for(auto& image:s.preparedImages)image.used=false;
    for(auto& image:s.frameImages)image.used=false;
    s.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);s.drawing=true;
    return true;
}
bool WidgetCanvas::Finish(WidgetBitmap& out){
    auto& s=*state_;if(!s.drawing)return false;
    while(s.clips)PopClip();
    s.drawing=false;
    if(FAILED(s.target->EndDraw())){s.images.clear();s.preparedImages.clear();s.frameImages.clear();s.target.Reset();s.brush.Reset();return false;}
    s.preparedImages.erase(std::remove_if(s.preparedImages.begin(),s.preparedImages.end(),
        [](const auto& image){return !image.used;}),s.preparedImages.end());
    s.frameImages.erase(std::remove_if(s.frameImages.begin(),s.frameImages.end(),
        [](const auto& image){return !image.used;}),s.frameImages.end());
    out.width=s.width;out.height=s.height;out.pixels.resize(static_cast<std::size_t>(s.width)*s.height);
    return SUCCEEDED(s.bitmap->CopyPixels(nullptr,s.width*4,static_cast<UINT>(out.pixels.size()*4),reinterpret_cast<BYTE*>(out.pixels.data())));
}
void WidgetCanvas::Text(const std::wstring& text,WidgetRect r,float size,std::uint32_t rgb,float alpha,bool semibold,WidgetTextAlign align){
    Text(text,r,size,rgb,alpha,semibold?WidgetTextWeight::Semibold:WidgetTextWeight::Normal,align);
}
void WidgetCanvas::Text(const std::wstring& text,WidgetRect r,float size,std::uint32_t rgb,float alpha,WidgetTextWeight weight,WidgetTextAlign align){
    auto& s=*state_;if(!s.drawing||text.empty()||r.width<=0||r.height<=0||!std::isfinite(size)||size<=0)return;
    const auto format=s.Format(size,weight,align,text);if(!format)return;
    const auto draw=[&](WidgetRect bounds){s.target->DrawText(text.data(),static_cast<UINT32>(text.size()),format,Rect(bounds),s.brush.value,D2D1_DRAW_TEXT_OPTIONS_CLIP);};
    // Keep glyph edges crisp at small sizes; the transparent surface already
    // provides enough contrast without a second offset glyph pass.
    s.Color(0,.12f*alpha);auto shadow=r;shadow.y+=1;draw(shadow);
    s.Color(rgb,alpha);draw(r);
}
float WidgetCanvas::MeasureText(const std::wstring& text,float size,bool semibold){
    return MeasureText(text,size,semibold?WidgetTextWeight::Semibold:WidgetTextWeight::Normal);
}
float WidgetCanvas::MeasureText(const std::wstring& text,float size,WidgetTextWeight weight){
    auto& s=*state_;if(!s.drawing||text.empty()||!std::isfinite(size)||size<=0)return 0;
    const auto format=s.Format(size,weight,WidgetTextAlign::Left,text);if(!format)return 0;
    Com<IDWriteTextLayout> layout;DWRITE_TEXT_METRICS metrics{};
    if(FAILED(s.factories->write->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format,100000,10000,layout.Put()))||
       FAILED(layout->GetMetrics(&metrics)))return 0;
    return metrics.widthIncludingTrailingWhitespace;
}
void WidgetCanvas::ScrollText(const std::wstring& text,WidgetRect viewport,float size,float offset,std::uint32_t rgb,float alpha){
    auto& s=*state_;if(!s.drawing||viewport.width<=0||viewport.height<=0||!std::isfinite(offset))return;
    const float width=std::max(viewport.width,MeasureText(text,size));
    offset=std::clamp(offset,0.f,std::max(0.f,width-viewport.width));
    s.target->PushAxisAlignedClip(Rect(viewport),D2D1_ANTIALIAS_MODE_ALIASED);
    Text(text,{viewport.x-offset,viewport.y,width,viewport.height},size,rgb,alpha);
    s.target->PopAxisAlignedClip();
}
bool WidgetCanvas::PushClip(WidgetRect bounds){
    auto& s=*state_;
    if(!s.drawing||!std::isfinite(bounds.x)||!std::isfinite(bounds.y)||
       !std::isfinite(bounds.width)||!std::isfinite(bounds.height)||bounds.width<0||bounds.height<0)return false;
    s.target->PushAxisAlignedClip(Rect(bounds),D2D1_ANTIALIAS_MODE_ALIASED);++s.clips;return true;
}
void WidgetCanvas::Offset(float dx,float dy){
    auto& s=*state_;if(!s.drawing||!std::isfinite(dx)||!std::isfinite(dy))return;
    s.target->SetTransform(D2D1::Matrix3x2F::Translation(dx,dy));
}
void WidgetCanvas::PopClip(){
    auto& s=*state_;if(!s.drawing||!s.clips)return;
    s.target->PopAxisAlignedClip();--s.clips;
}
void WidgetCanvas::Rounded(WidgetRect r,float radius,std::uint32_t rgb,float alpha){
    auto& s=*state_;if(!s.drawing||r.width<=0||r.height<=0)return;
    s.Color(rgb,alpha);s.target->FillRoundedRectangle(D2D1::RoundedRect(Rect(r),radius,radius),s.brush.value);
}
void WidgetCanvas::Ellipse(WidgetRect r,std::uint32_t rgb,float alpha){
    auto& s=*state_;if(!s.drawing||r.width<=0||r.height<=0)return;
    s.Color(rgb,alpha);
    s.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x+r.width/2,r.y+r.height/2),r.width/2,r.height/2),s.brush.value);
}
void WidgetCanvas::AnnularSector(float centerX,float centerY,float innerRadius,float outerRadius,
                                float startDegrees,float sweepDegrees,std::uint32_t rgb,float alpha){
    auto& s=*state_;
    if(!s.drawing||!std::isfinite(centerX)||!std::isfinite(centerY)||
       !std::isfinite(innerRadius)||!std::isfinite(outerRadius)||
       !std::isfinite(startDegrees)||!std::isfinite(sweepDegrees)||!std::isfinite(alpha)||
       innerRadius<0||outerRadius<=innerRadius||sweepDegrees<=0||sweepDegrees>360)return;
    Com<ID2D1PathGeometry> geometry;Com<ID2D1GeometrySink> sink;
    if(FAILED(s.factories->draw->CreatePathGeometry(geometry.Put()))||FAILED(geometry->Open(sink.Put())))return;
    startDegrees=std::remainder(startDegrees,360.f);
    const auto point=[&](float radius,float degrees){
        const float angle=degrees*3.14159265358979323846f/180;
        return D2D1::Point2F(centerX+std::sin(angle)*radius,centerY-std::cos(angle)*radius);
    };
    const int arcs=static_cast<int>(std::ceil(sweepDegrees/180));
    const float step=sweepDegrees/arcs;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(point(outerRadius,startDegrees),D2D1_FIGURE_BEGIN_FILLED);
    for(int i=1;i<=arcs;++i)sink->AddArc(D2D1::ArcSegment(point(outerRadius,startDegrees+i*step),
        D2D1::SizeF(outerRadius,outerRadius),0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
    if(innerRadius>0){
        sink->AddLine(point(innerRadius,startDegrees+sweepDegrees));
        for(int i=arcs-1;i>=0;--i)sink->AddArc(D2D1::ArcSegment(point(innerRadius,startDegrees+i*step),
            D2D1::SizeF(innerRadius,innerRadius),0,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
    }else sink->AddLine(D2D1::Point2F(centerX,centerY));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if(FAILED(sink->Close()))return;
    s.Color(rgb,alpha);s.target->FillGeometry(geometry.value,s.brush.value);
}
void WidgetCanvas::Polygon(const std::vector<float>& points,std::uint32_t rgb,float alpha,float round){
    auto& s=*state_;
    if(!s.drawing||points.size()<6||points.size()%2||!std::isfinite(alpha)||!std::isfinite(round)||round<0)return;
    for(const float value:points)if(!std::isfinite(value))return;
    Com<ID2D1PathGeometry> geometry;Com<ID2D1GeometrySink> sink;
    if(FAILED(s.factories->draw->CreatePathGeometry(geometry.Put()))||FAILED(geometry->Open(sink.Put())))return;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(points[0],points[1]),D2D1_FIGURE_BEGIN_FILLED);
    for(std::size_t i=2;i<points.size();i+=2)sink->AddLine(D2D1::Point2F(points[i],points[i+1]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if(FAILED(sink->Close()))return;
    s.Color(rgb,alpha);
    if(round<=0){s.target->FillGeometry(geometry.value,s.brush.value);return;}
    // Fill and outline in one layer so the overlap is not drawn twice.
    Com<ID2D1StrokeStyle> stroke;
    if(FAILED(s.factories->draw->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND),nullptr,0,stroke.Put()))){s.target->FillGeometry(geometry.value,s.brush.value);return;}
    Com<ID2D1PathGeometry> outline;Com<ID2D1GeometrySink> widened;
    if(SUCCEEDED(s.factories->draw->CreatePathGeometry(outline.Put()))&&SUCCEEDED(outline->Open(widened.Put()))&&
       SUCCEEDED(geometry->Widen(round,stroke.value,nullptr,D2D1_DEFAULT_FLATTENING_TOLERANCE,widened.value))&&
       SUCCEEDED(widened->Close())){
        Com<ID2D1PathGeometry> merged;Com<ID2D1GeometrySink> union_;
        if(SUCCEEDED(s.factories->draw->CreatePathGeometry(merged.Put()))&&SUCCEEDED(merged->Open(union_.Put()))&&
           SUCCEEDED(geometry->CombineWithGeometry(outline.value,D2D1_COMBINE_MODE_UNION,nullptr,union_.value))&&
           SUCCEEDED(union_->Close())){s.target->FillGeometry(merged.value,s.brush.value);return;}
    }
    s.target->FillGeometry(geometry.value,s.brush.value);
}
void WidgetCanvas::Line(float x,float y,float endX,float endY,std::uint32_t rgb,float alpha,float thickness){
    auto& s=*state_;if(!s.drawing)return;
    s.Color(rgb,alpha);s.target->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(endX,endY),s.brush.value,thickness);
}
std::uint64_t WidgetCanvas::SurfaceGeneration() const{return state_->generation;}
std::size_t WidgetCanvas::CachedImages() const{return state_->images.size();}
std::size_t WidgetCanvas::CachedImageVariants() const{
    std::size_t count=0;for(const auto& entry:state_->images)count+=entry.second.variants.size();return count;
}
std::size_t WidgetCanvas::CachedBitmaps() const{return state_->preparedImages.size();}
bool WidgetCanvas::ImageBitmap(std::shared_ptr<const WidgetBitmap> pixels,WidgetRect bounds,float opacity){
    auto& s=*state_;
    if(!s.drawing||!pixels||!pixels->width||!pixels->height||pixels->width>256||pixels->height>256||
       pixels->pixels.size()!=static_cast<std::size_t>(pixels->width)*pixels->height||
       !std::isfinite(bounds.width)||!std::isfinite(bounds.height)||bounds.width<=0||bounds.height<=0||!std::isfinite(opacity))return false;
    auto found=std::find_if(s.preparedImages.begin(),s.preparedImages.end(),
        [&](const auto& image){return image.pixels==pixels;});
    if(found==s.preparedImages.end()){
        if(s.preparedImages.size()>=128){
            const auto unused=std::find_if(s.preparedImages.begin(),s.preparedImages.end(),
                [](const auto& image){return !image.used;});
            if(unused==s.preparedImages.end())return false;
            s.preparedImages.erase(unused);
        }
        auto image=std::make_unique<Com<ID2D1Bitmap>>();
        const auto props=D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(s.target->CreateBitmap(D2D1::SizeU(pixels->width,pixels->height),pixels->pixels.data(),
            pixels->width*4,props,image->Put())))return false;
        s.preparedImages.push_back({std::move(pixels),std::move(image),true});
        found=std::prev(s.preparedImages.end());
    }
    found->used=true;
    const auto size=found->bitmap->value->GetSize();
    const float scale=std::min(bounds.width/size.width,bounds.height/size.height);
    const float width=size.width*scale,height=size.height*scale;
    bounds.x+=(bounds.width-width)/2;bounds.y+=(bounds.height-height)/2;
    bounds.width=width;bounds.height=height;
    const auto alpha=std::clamp(opacity,0.f,1.f);const auto destination=Rect(bounds);
    // Icons are cached a little larger than drawn; high-quality cubic keeps
    // them sharp when scaled down. Older render targets fall back to linear.
    Com<ID2D1DeviceContext> context;
    if(SUCCEEDED(s.target->QueryInterface(__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(context.Put())))&&context.value)
        context->DrawBitmap(found->bitmap->value,&destination,alpha,D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,nullptr,nullptr);
    else s.target->DrawBitmap(found->bitmap->value,destination,alpha,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}
bool WidgetCanvas::ImageFrame(std::shared_ptr<const WidgetBitmap> pixels,WidgetRect bounds,float opacity){
    auto& s=*state_;
    if(!s.drawing||!pixels||!pixels->width||!pixels->height||pixels->width>8192||pixels->height>8192||
       static_cast<std::uint64_t>(pixels->width)*pixels->height>16777216||
       pixels->pixels.size()!=static_cast<std::size_t>(pixels->width)*pixels->height||
       !std::isfinite(bounds.x)||!std::isfinite(bounds.y)||!std::isfinite(bounds.width)||!std::isfinite(bounds.height)||
       bounds.width<=0||bounds.height<=0||!std::isfinite(opacity))return false;
    auto found=std::find_if(s.frameImages.begin(),s.frameImages.end(),[&](const auto& image){return image.pixels==pixels;});
    if(found==s.frameImages.end()){
        if(s.frameImages.size()>=3){
            const auto unused=std::find_if(s.frameImages.begin(),s.frameImages.end(),[](const auto& image){return !image.used;});
            if(unused==s.frameImages.end())return false;
            s.frameImages.erase(unused);
        }
        auto image=std::make_unique<Com<ID2D1Bitmap>>();
        const auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(s.target->CreateBitmap(D2D1::SizeU(pixels->width,pixels->height),pixels->pixels.data(),pixels->width*4,props,image->Put())))return false;
        s.frameImages.push_back({std::move(pixels),std::move(image),true});found=std::prev(s.frameImages.end());
    }
    found->used=true;
    s.target->DrawBitmap(found->bitmap->value,Rect(bounds),std::clamp(opacity,0.f,1.f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}
bool WidgetCanvas::ImageResource(unsigned id,WidgetRect bounds){
    auto& s=*state_;
    if(!s.drawing||!id||id>65535||!std::isfinite(bounds.x)||!std::isfinite(bounds.y)||
        !std::isfinite(bounds.width)||!std::isfinite(bounds.height)||bounds.width<=0||bounds.height<=0||
        !std::isfinite(bounds.x+bounds.width)||!std::isfinite(bounds.y+bounds.height))return false;
    auto found=s.images.find(id);
    if(found==s.images.end()&&s.images.size()>=16)return false;
    UINT sourceWidth=found==s.images.end()?0:found->second.width;
    UINT sourceHeight=found==s.images.end()?0:found->second.height;
    UINT rasterWidth=0,rasterHeight=0;
    WidgetRect destination;
    const auto fit=[&]{
        const float sourceW=static_cast<float>(sourceWidth),sourceH=static_cast<float>(sourceHeight);
        const float scale=std::min(bounds.width/sourceW,bounds.height/sourceH);
        const float width=sourceW*scale,height=sourceH*scale;
        destination={bounds.x+(bounds.width-width)/2,bounds.y+(bounds.height-height)/2,width,height};
        // Filtering works on physical pixels. Fractional DIP bounds need the
        // next whole pixel; D2D only performs the final subpixel placement.
        rasterWidth=static_cast<UINT>(std::clamp(std::ceil(static_cast<double>(width)*s.dpi/96),1.0,static_cast<double>(sourceWidth)));
        rasterHeight=static_cast<UINT>(std::clamp(std::ceil(static_cast<double>(height)*s.dpi/96),1.0,static_cast<double>(sourceHeight)));
    };
    ID2D1Bitmap* image=nullptr;
    if(found!=s.images.end()){
        fit();
        for(const auto& variant:found->second.variants)
            if(variant.width==rasterWidth&&variant.height==rasterHeight){image=variant.bitmap->value;break;}
    }
    if(!image){
        const auto module=GetModuleHandleW(nullptr);
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
        const DWORD bytes=resource?SizeofResource(module,resource):0;
        const auto data=resource?LoadResource(module,resource):nullptr;
        auto* buffer=data?static_cast<BYTE*>(LockResource(data)):nullptr;
        if(!buffer||!bytes||bytes>1024*1024)return false;
        Com<IWICStream> stream;Com<IWICBitmapDecoder> decoder;
        Com<IWICBitmapFrameDecode> frame;Com<IWICFormatConverter> converter;Com<IWICBitmapScaler> scaler;
        if(FAILED(s.factories->imaging->CreateStream(stream.Put()))||
            FAILED(stream->InitializeFromMemory(buffer,bytes))||
            FAILED(s.factories->imaging->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,decoder.Put()))||
            FAILED(decoder->GetFrame(0,frame.Put()))||FAILED(frame->GetSize(&sourceWidth,&sourceHeight))||
            !sourceWidth||!sourceHeight||sourceWidth>1024||sourceHeight>1024||
            FAILED(s.factories->imaging->CreateFormatConverter(converter.Put()))||
            FAILED(converter->Initialize(frame.value,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        fit();
        IWICBitmapSource* pixels=converter.value;
        if(rasterWidth<sourceWidth||rasterHeight<sourceHeight){
            // Fant integrates source-pixel coverage while shrinking. Convert
            // to premultiplied alpha first so transparent RGB cannot fringe.
            if(FAILED(s.factories->imaging->CreateBitmapScaler(scaler.Put()))||
                FAILED(scaler->Initialize(pixels,rasterWidth,rasterHeight,WICBitmapInterpolationModeFant)))return false;
            pixels=scaler.value;
        }
        auto bitmap=std::make_unique<Com<ID2D1Bitmap>>();
        const auto properties=D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(s.target->CreateBitmapFromWicBitmap(pixels,&properties,bitmap->Put())))return false;
        if(found==s.images.end())found=s.images.emplace(id,State::ResourceImage{sourceWidth,sourceHeight,{}}).first;
        auto& variants=found->second.variants;
        if(variants.size()==2)variants.erase(variants.begin());
        image=bitmap->value;
        variants.push_back({rasterWidth,rasterHeight,std::move(bitmap)});
    }
    s.target->DrawBitmap(image,Rect(destination),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}
}
