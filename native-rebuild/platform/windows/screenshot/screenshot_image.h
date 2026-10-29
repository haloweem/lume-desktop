#pragma once
#include "core/screenshot/editor_model.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lume::platform::windows::capture {
struct Image {
    int width=0,height=0;
    std::vector<std::uint32_t> pixels;
    bool Valid()const{return width>0&&height>0&&pixels.size()==static_cast<std::size_t>(width)*height;}
};
// All coordinates are physical pixels. These functions never upload data.
bool GrabDesktop(Image& output,POINT& origin);
Image Crop(const Image&,screenshot::Rect);
bool WritePng(const Image&,const std::wstring& path);
bool CopyImage(HWND owner,const Image&);
// false on cancellation or error; the caller retains the editor and image.
bool SaveImage(HWND owner,const Image&);
}
