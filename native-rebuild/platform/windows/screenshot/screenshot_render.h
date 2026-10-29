#pragma once
#include "platform/windows/screenshot/screenshot_image.h"
#include <objidl.h>
#include <gdiplus.h>

namespace lume::platform::windows::capture {
// clip is the stable selection, not the repaint damage rectangle. Coordinates are image pixels.
// Restore the previous pending bounds from the committed image before rendering a new pending stroke.
// Bounds include filter sample halos as well as writes; scratch allocation failure may throw.
void RenderAnnotation(Image&,const screenshot::Annotation&,screenshot::Rect clip);
void RenderAnnotation(Image&,const Image& original,const screenshot::Annotation&,screenshot::Rect clip);
screenshot::Rect AnnotationBounds(const screenshot::Annotation&,screenshot::Rect clip);
Image Composite(const Image&,const std::vector<screenshot::Annotation>&,screenshot::Rect clip);
enum class Command { Shape,Arrow,Pen,Marker,Mosaic,Text,Eraser,Undo,Redo,Close,Pin,Save,Copy };
inline constexpr int CommandCount=13;
void DrawIcon(Gdiplus::Graphics&,Command,float x,float y,float size,Gdiplus::Color);
const wchar_t* CommandName(Command);
}
