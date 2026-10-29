#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lume::platform::windows {
struct WidgetRect { float x=0,y=0,width=0,height=0; };
struct WidgetBitmap { unsigned width=0,height=0;std::vector<std::uint32_t> pixels; };
enum class WidgetTextAlign { Left,Center,Right };
enum class WidgetTextWeight { Normal=400,Medium=500,Semibold=600,Bold=700 };
// Transparent foreground only. Glass is owned by WidgetMaterialRuntime.
// A live canvas retains its bitmap/brush and bounded text formats between draws;
// factories are weakly shared and released with the last canvas.
class WidgetCanvas final {
public:
    WidgetCanvas();
    ~WidgetCanvas();
    WidgetCanvas(const WidgetCanvas&)=delete;
    WidgetCanvas& operator=(const WidgetCanvas&)=delete;
    bool Begin(float width,float height,unsigned dpi);
    bool Finish(WidgetBitmap&);
    void Text(const std::wstring&,WidgetRect,float size,std::uint32_t rgb=0xffffff,
              float alpha=1,bool semibold=false,WidgetTextAlign align=WidgetTextAlign::Left);
    void Text(const std::wstring&,WidgetRect,float size,std::uint32_t rgb,
              float alpha,WidgetTextWeight,WidgetTextAlign align=WidgetTextAlign::Left);
    float MeasureText(const std::wstring&,float size,bool semibold=false);
    float MeasureText(const std::wstring&,float size,WidgetTextWeight);
    void ScrollText(const std::wstring&,WidgetRect viewport,float size,float offset,
                    std::uint32_t rgb=0xffffff,float alpha=1);
    // Direct2D clip stack only; no extra surface, buffer or worker.
    bool PushClip(WidgetRect);
    void PopClip();
    // Shifts later drawing by (dx, dy) DIPs until the next Offset or Begin.
    void Offset(float dx,float dy);
    void Rounded(WidgetRect,float radius,std::uint32_t rgb,float alpha=1);
    // The ellipse inscribed in the rect.
    void Ellipse(WidgetRect,std::uint32_t rgb,float alpha=1);
    // Clockwise degrees from twelve o'clock; a 360-degree sweep draws a ring.
    void AnnularSector(float centerX,float centerY,float innerRadius,float outerRadius,
                       float startDegrees,float sweepDegrees,std::uint32_t rgb,float alpha=1);
    void Line(float x,float y,float endX,float endY,std::uint32_t rgb,float alpha=1,float thickness=1);
    // A filled polygon of points {x0,y0,x1,y1,...}; round > 0 softens its
    // corners with a round-joined outline of that width (growing it by round/2).
    void Polygon(const std::vector<float>& points,std::uint32_t rgb,float alpha=1,float round=0);
    // PNG resources are prefiltered to their physical draw size. A canvas
    // retains at most 16 resources and two raster sizes per resource.
    bool ImageResource(unsigned id,WidgetRect);
    // Immutable premultiplied BGRA pixels prepared by the caller. No file I/O.
    bool ImageBitmap(std::shared_ptr<const WidgetBitmap>, WidgetRect, float opacity = 1);
    // Bounded retained UI frames (rather than 256px icons), stretched into bounds.
    bool ImageFrame(std::shared_ptr<const WidgetBitmap>, WidgetRect, float opacity = 1);
    std::size_t CachedImages()const;
    std::size_t CachedImageVariants()const;
    std::size_t CachedBitmaps()const;
    std::uint64_t SurfaceGeneration() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
