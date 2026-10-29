#include "platform/windows/screenshot/screenshot_image.h"
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace lume::platform::windows::capture {
namespace {
// Bound each uncompressed image to 256 MB before allocating or using GDI strides.
constexpr std::size_t MaxPixels=64000000u;
bool Dimensions(int width,int height){
    return width>0&&height>0&&width<=(std::numeric_limits<int>::max)()/4&&
        static_cast<std::size_t>(width)<=MaxPixels/static_cast<std::size_t>(height);
}
bool Valid(const Image& image){return Dimensions(image.width,image.height)&&image.Valid();}
struct PhysicalCoordinates {
    DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~PhysicalCoordinates(){if(previous)SetThreadDpiAwarenessContext(previous);}
};
struct DesktopBitmap {
    HDC desktop=nullptr,memory=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    ~DesktopBitmap(){
        if(previous)SelectObject(memory,previous);
        if(bitmap)DeleteObject(bitmap);
        if(memory)DeleteDC(memory);
        if(desktop)ReleaseDC(nullptr,desktop);
    }
};
struct Stream {
    IStream* value=nullptr;
    ~Stream(){if(value)value->Release();}
};
struct TemporaryFile {
    std::wstring path;
    HANDLE handle=INVALID_HANDLE_VALUE;
    bool owned=false;
    ~TemporaryFile(){
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        if(owned)DeleteFileW(path.c_str());
    }
};
struct GlobalBlock {
    HGLOBAL value=nullptr;
    ~GlobalBlock(){if(value)GlobalFree(value);}
};
struct Clipboard {
    bool opened=false;
    ~Clipboard(){if(opened)CloseClipboard();}
};
bool PngEncoder(CLSID& result){
    UINT count=0,bytes=0;
    if(Gdiplus::GetImageEncodersSize(&count,&bytes)!=Gdiplus::Ok||!count||!bytes||bytes>1024u*1024u)return false;
    auto storage=std::make_unique<BYTE[]>(bytes);
    auto* encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.get());
    if(Gdiplus::GetImageEncoders(count,bytes,encoders)!=Gdiplus::Ok)return false;
    for(UINT i=0;i<count;++i)if(encoders[i].MimeType&&std::wcscmp(encoders[i].MimeType,L"image/png")==0){
        result=encoders[i].Clsid;return true;
    }
    return false;
}
}

bool GrabDesktop(Image& output,POINT& origin){
    try {
        PhysicalCoordinates coordinates;
        if(!coordinates.previous)return false;
        const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const POINT topLeft{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN)};
        if(!Dimensions(width,height))return false;
        Image captured{width,height,{}};
        captured.pixels.resize(static_cast<std::size_t>(width)*static_cast<std::size_t>(height));
        DesktopBitmap surface;
        surface.desktop=GetDC(nullptr);if(!surface.desktop)return false;
        surface.memory=CreateCompatibleDC(surface.desktop);if(!surface.memory)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        surface.bitmap=CreateDIBSection(surface.desktop,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!surface.bitmap||!bits)return false;
        const auto previous=SelectObject(surface.memory,surface.bitmap);
        if(!previous||previous==HGDI_ERROR)return false;
        surface.previous=previous;
        if(!BitBlt(surface.memory,0,0,width,height,surface.desktop,topLeft.x,topLeft.y,SRCCOPY|CAPTUREBLT)||!GdiFlush())return false;
        const auto* pixels=static_cast<const std::uint32_t*>(bits);
        for(std::size_t i=0;i<captured.pixels.size();++i)captured.pixels[i]=pixels[i]|0xff000000u;
        output=std::move(captured);origin=topLeft;return true;
    }catch(const std::exception&){return false;}
}

Image Crop(const Image& image,screenshot::Rect rect){
    if(!Valid(image))return {};
    rect.left=std::clamp(rect.left,0,image.width);rect.right=std::clamp(rect.right,0,image.width);
    rect.top=std::clamp(rect.top,0,image.height);rect.bottom=std::clamp(rect.bottom,0,image.height);
    if(rect.right<=rect.left||rect.bottom<=rect.top)return {};
    try {
        Image result{rect.right-rect.left,rect.bottom-rect.top,{}};
        const auto width=static_cast<std::size_t>(result.width),stride=static_cast<std::size_t>(image.width);
        result.pixels.resize(width*static_cast<std::size_t>(result.height));
        for(int y=0;y<result.height;++y){
            const auto source=static_cast<std::size_t>(rect.top+y)*stride+static_cast<std::size_t>(rect.left);
            std::copy_n(image.pixels.data()+source,width,result.pixels.data()+static_cast<std::size_t>(y)*width);
        }
        return result;
    }catch(const std::exception&){return {};}
}

bool WritePng(const Image& image,const std::wstring& path){
    if(!Valid(image)||path.empty()||path.find(L'\0')!=std::wstring::npos)return false;
    try {
        const auto target=std::filesystem::absolute(std::filesystem::path(path));
        if(!target.has_filename()||target.filename()==L"."||target.filename()==L".."||
           target.filename().native().find(L':')!=std::wstring::npos)return false;
        CLSID encoder{};if(!PngEncoder(encoder))return false;
        Stream stream;
        if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream.value)))return false;
        Gdiplus::Bitmap bitmap(image.width,image.height,image.width*4,PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())));
        if(bitmap.GetLastStatus()!=Gdiplus::Ok||bitmap.Save(stream.value,&encoder,nullptr)!=Gdiplus::Ok)return false;
        STATSTG stat{};
        if(FAILED(stream.value->Stat(&stat,STATFLAG_NONAME))||!stat.cbSize.QuadPart||stat.cbSize.QuadPart>MAXDWORD)return false;
        LARGE_INTEGER start{};if(FAILED(stream.value->Seek(start,STREAM_SEEK_SET,nullptr)))return false;
        GUID guid{};wchar_t id[40]{};
        if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,id,40))return false;
        TemporaryFile temp;
        temp.path=(target.parent_path()/(std::wstring(L".lume-screenshot-")+id+L".tmp")).wstring();
        temp.handle=CreateFileW(temp.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(temp.handle==INVALID_HANDLE_VALUE)return false;
        temp.owned=true;
        std::array<BYTE,65536> buffer{};
        auto remaining=stat.cbSize.QuadPart;
        while(remaining){
            const auto requested=static_cast<ULONG>((std::min)(remaining,static_cast<ULONGLONG>(buffer.size())));
            ULONG read=0;DWORD written=0;
            if(FAILED(stream.value->Read(buffer.data(),requested,&read))||read!=requested||
               !WriteFile(temp.handle,buffer.data(),read,&written,nullptr)||written!=read)return false;
            remaining-=read;
        }
        if(!FlushFileBuffers(temp.handle))return false;
        const BOOL closed=CloseHandle(temp.handle);temp.handle=INVALID_HANDLE_VALUE;
        if(!closed)return false;
        // Same-directory rename publishes only the complete, flushed image.
        if(!MoveFileExW(temp.path.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
        temp.owned=false;return true;
    }catch(const std::exception&){return false;}
}

bool CopyImage(HWND owner,const Image& image){
    if(!owner||!IsWindow(owner)||!Valid(image))return false;
    const auto pixelBytes=image.pixels.size()*sizeof(std::uint32_t);
    GlobalBlock block{GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPINFOHEADER)+pixelBytes)};
    if(!block.value)return false;
    auto* data=static_cast<BYTE*>(GlobalLock(block.value));if(!data)return false;
    BITMAPINFOHEADER header{};header.biSize=static_cast<DWORD>(sizeof(header));
    // CF_DIB consumers are not consistent about top-down DIBs. Publish the
    // conventional bottom-up layout and keep the editor's top-down pixels
    // unchanged by reversing rows while copying into the clipboard block.
    header.biWidth=image.width;header.biHeight=image.height;header.biPlanes=1;
    header.biBitCount=32;header.biCompression=BI_RGB;header.biSizeImage=static_cast<DWORD>(pixelBytes);
    std::memcpy(data,&header,sizeof(header));
    auto* destination=reinterpret_cast<std::uint32_t*>(data+sizeof(header));
    for(int y=0;y<image.height;++y){
        const auto sourceRow=static_cast<std::size_t>(image.height-1-y)*static_cast<std::size_t>(image.width);
        std::memcpy(destination+static_cast<std::size_t>(y)*static_cast<std::size_t>(image.width),
                    image.pixels.data()+sourceRow,static_cast<std::size_t>(image.width)*sizeof(std::uint32_t));
    }
    GlobalUnlock(block.value);
    // Prepare the complete movable DIB before acquiring or emptying the clipboard.
    Clipboard clipboard{OpenClipboard(owner)!=FALSE};if(!clipboard.opened)return false;
    if(!EmptyClipboard()||!SetClipboardData(CF_DIB,block.value))return false;
    block.value=nullptr;return true;
}

bool SaveImage(HWND owner,const Image& image){
    if(!owner||!IsWindow(owner)||!Valid(image)){SetLastError(ERROR_GEN_FAILURE);return false;}
    wchar_t path[32768]=L"Screenshot.png";
    OPENFILENAMEW dialog{};dialog.lStructSize=static_cast<DWORD>(sizeof(dialog));dialog.hwndOwner=owner;
    dialog.lpstrFilter=L"PNG image (*.png)\0*.png\0\0";dialog.nFilterIndex=1;
    dialog.lpstrFile=path;dialog.nMaxFile=static_cast<DWORD>(sizeof(path)/sizeof(path[0]));
    dialog.lpstrDefExt=L"png";
    dialog.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;
    // The editor stays open for either false result; only cancellation suppresses its error UI.
    if(!GetSaveFileNameW(&dialog)){
        SetLastError(CommDlgExtendedError()==0?ERROR_CANCELLED:ERROR_GEN_FAILURE);return false;
    }
    const bool saved=WritePng(image,path);
    SetLastError(saved?ERROR_SUCCESS:ERROR_GEN_FAILURE);return saved;
}
}
