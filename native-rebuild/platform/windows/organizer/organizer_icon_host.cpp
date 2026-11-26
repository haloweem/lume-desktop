#include "platform/windows/organizer/organizer_icon_protocol.h"
#include "platform/windows/organizer/shell_open.h"
#include "platform/windows/windows_settings.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <wincodec.h>
#include <algorithm>
#include <cwchar>
#include <string_view>
#include <vector>

namespace {
using namespace lume::platform::windows::organizer_icon_ipc;
template<class T>struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
    T** Put(){return &value;}
    T* operator->()const{return value;}
};
struct Icon {HICON value=nullptr;~Icon(){if(value)DestroyIcon(value);}};
struct Bitmap {HBITMAP value=nullptr;~Bitmap(){if(value)DeleteObject(value);}};
HANDLE Inherited(const wchar_t* argument) {
    if(!argument||!*argument)return nullptr;
    for(auto* p=argument;*p;++p)if(*p<L'0'||*p>L'9')return nullptr;
    wchar_t* end=nullptr;
    const auto value=wcstoull(argument,&end,10);
    if(!end||*end||!value||value>UINTPTR_MAX)return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}
bool CopyPixels(IWICImagingFactory* factory,IWICBitmapSource* source,Message& message) {
    Com<IWICFormatConverter> converted;
    UINT width=0,height=0;
    if(FAILED(source->GetSize(&width,&height))||!width||!height||width>MaxSide||height>MaxSide||
       FAILED(factory->CreateFormatConverter(converted.Put()))||
       FAILED(converted->Initialize(source,GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||
       FAILED(converted->CopyPixels(nullptr,width*4,width*height*4,
            reinterpret_cast<BYTE*>(message.pixels))))return false;
    bool visible=false;
    for(std::size_t i=0;i<static_cast<std::size_t>(width)*height;++i) {
        const auto color=message.pixels[i],alpha=color>>24;
        if(((color>>16)&255)>alpha||((color>>8)&255)>alpha||(color&255)>alpha)return false;
        visible=visible||alpha!=0;
    }
    if(!visible)return false;
    message.width=width;message.height=height;return true;
}
bool ShellImage(IWICImagingFactory* factory,const wchar_t* path,Message& message) {
#ifdef LUME_ICON_TEST_FIXTURE
    // Exercise the native resource/target fallbacks without changing Shell
    // registrations or the user's shortcuts.
    if(std::wstring_view(path).find(L"shell-unavailable")!=std::wstring_view::npos)return false;
#endif
    Com<IShellItemImageFactory> image;Com<IWICBitmap> source;Bitmap bitmap;
    const auto attributes=GetFileAttributesW(path);
    const bool folder=attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY);
    const auto flags=folder?SIIGBF_RESIZETOFIT:SIIGBF_ICONONLY;
    // Match the original organizer: the shell item owns custom shortcut icon
    // indexes and folder artwork. SHGFI_ICON bakes in a shortcut arrow and
    // returns the system large-icon metric, which is too small for 44 DIP.
    if(FAILED(SHCreateItemFromParsingName(path,nullptr,IID_PPV_ARGS(image.Put())))||
        FAILED(image->GetImage({static_cast<LONG>(message.requestedSide),
            static_cast<LONG>(message.requestedSide)},flags,&bitmap.value))||!bitmap.value)return false;
    if(SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap.value,nullptr,
        WICBitmapUsePremultipliedAlpha,source.Put()))&&CopyPixels(factory,source.value,message))return true;
    // Some folder thumbnail providers return straight-alpha pixels. Keep the
    // usual Shell icon bytes unchanged, but normalize that representation
    // when the premultiplied validation fails.
    Com<IWICBitmap> straightAlpha;
    return SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap.value,nullptr,
        WICBitmapUseAlpha,straightAlpha.Put()))&&CopyPixels(factory,straightAlpha.value,message);
}
bool ResourceIcon(IWICImagingFactory* factory,const wchar_t* path,int index,Message& message) {
    wchar_t expanded[MaxPath]{};
    const auto length=ExpandEnvironmentStringsW(path,expanded,static_cast<DWORD>(MaxPath));
    if(!length||length>MaxPath)return false;
    Icon icon;Com<IWICBitmap> source;
    return SUCCEEDED(SHDefExtractIconW(expanded,index,0,&icon.value,nullptr,message.requestedSide))&&icon.value&&
        SUCCEEDED(factory->CreateBitmapFromHICON(icon.value,source.Put()))&&
        CopyPixels(factory,source.value,message);
}
bool HasExtension(const wchar_t* path,const wchar_t* extension) {
    const auto* dot=wcsrchr(path,L'.');
    return dot&&_wcsicmp(dot,extension)==0;
}
bool NativeAppIcon(const wchar_t* path) {
    return HasExtension(path,L".exe")||HasExtension(path,L".ico");
}
bool ExtractImage(IWICImagingFactory* factory,Message& message) {
    using namespace lume::platform::windows;
    if (_wcsicmp(message.path, WindowsSettingsImage().c_str()) == 0) {
        const auto app = std::wstring(L"shell:AppsFolder\\") + WindowsSettingsAppId;
        if (ShellImage(factory, app.c_str(), message)) return true;
    }
    if(HasExtension(message.path,L".lnk")) {
        Com<IShellLinkW> shortcut;Com<IPersistFile> file;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(shortcut.Put())))&&
           SUCCEEDED(shortcut->QueryInterface(IID_PPV_ARGS(file.Put())))&&
           SUCCEEDED(file->Load(message.path,STGM_READ))) {
            wchar_t target[MaxPath]{};int index=0;
            const bool hasTarget=SUCCEEDED(shortcut->GetPath(target,static_cast<int>(MaxPath),nullptr,SLGP_RAWPATH))&&*target;
            wchar_t icon[MaxPath]{};
            const bool hasCustom=SUCCEEDED(shortcut->GetIconLocation(icon,static_cast<int>(MaxPath),&index))&&*icon;
            // The Shell image list can wrap undersized app icons in a faint
            // square. Read the actual resource, retaining custom icon indices.
            if(hasCustom&&ResourceIcon(factory,icon,index,message))return true;
            if(!hasCustom&&hasTarget&&NativeAppIcon(target)&&ResourceIcon(factory,target,0,message))return true;
            if(ShellImage(factory,message.path,message))return true;
            return hasTarget&&(ShellImage(factory,target,message)||ResourceIcon(factory,target,0,message));
        }
    }
    if(NativeAppIcon(message.path)&&ResourceIcon(factory,message.path,0,message))return true;
    return ShellImage(factory,message.path,message)||ResourceIcon(factory,message.path,0,message);
}
void NormalizeArtwork(IWICImagingFactory* factory,Message& message) {
    const unsigned width=message.width,height=message.height,side=message.requestedSide;
    unsigned left=width,top=height,right=0,bottom=0;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        if((message.pixels[y*width+x]>>24)<48)continue;
        left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
    }
    if(right<=left||bottom<=top)return;
    const auto artwork=std::max(right-left,bottom-top);
    const auto canvas=std::max(width,height);
    if(artwork*100>=canvas*86)return;
    // Keep a small margin for antialiasing and shadow around the visible art.
    const unsigned pad=std::max(1u,artwork/32);
    left=left>pad?left-pad:0;top=top>pad?top-pad:0;
    right=std::min(width,right+pad);bottom=std::min(height,bottom+pad);
    const unsigned cropWidth=right-left,cropHeight=bottom-top;
    const unsigned longest=std::max(cropWidth,cropHeight);
    const unsigned outWidth=std::max(1u,(cropWidth*side+longest/2)/longest);
    const unsigned outHeight=std::max(1u,(cropHeight*side+longest/2)/longest);
    Com<IWICBitmap> bitmap;Com<IWICBitmapClipper> clip;Com<IWICBitmapScaler> scaler;
    WICRect crop{static_cast<INT>(left),static_cast<INT>(top),static_cast<INT>(cropWidth),static_cast<INT>(cropHeight)};
    if(FAILED(factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppPBGRA,width*4,width*height*4,
            reinterpret_cast<BYTE*>(message.pixels),bitmap.Put()))||
       FAILED(factory->CreateBitmapClipper(clip.Put()))||FAILED(clip->Initialize(bitmap.value,&crop))||
       FAILED(factory->CreateBitmapScaler(scaler.Put()))||
       FAILED(scaler->Initialize(clip.value,outWidth,outHeight,WICBitmapInterpolationModeFant)))return;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(outWidth)*outHeight);
    if(FAILED(scaler->CopyPixels(nullptr,outWidth*4,outWidth*outHeight*4,reinterpret_cast<BYTE*>(pixels.data()))))return;
    std::copy(pixels.begin(),pixels.end(),message.pixels);message.width=outWidth;message.height=outHeight;
}
bool Extract(Message& message) {
    Com<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.Put())))||!ExtractImage(factory.value,message))return false;
    NormalizeArtwork(factory.value,message);
    return true;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(args&&count==3&&_wcsicmp(args[1],L"--open")==0) {
        // Hand the foreground right on to whatever the shell starts.
        AllowSetForegroundWindow(ASFW_ANY);
        const auto opened=lume::platform::windows::OpenShellTarget(args[2]);
        LocalFree(args);return opened?0:10;
    }
    if(!args||count!=5){if(args)LocalFree(args);return 2;}
    const HANDLE mapping=Inherited(args[1]),request=Inherited(args[2]),
        reply=Inherited(args[3]),quit=Inherited(args[4]);
    LocalFree(args);
    if(!mapping||!request||!reply||!quit)return 2;
    auto* message=static_cast<Message*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Message)));
    if(!message)return 3;
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))){UnmapViewOfFile(message);return 4;}
    int result=0;const HANDLE waits[]{quit,request};
    for(;;) {
        const auto status=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
        if(status==WAIT_OBJECT_0)break;
        if(status!=WAIT_OBJECT_0+1){result=5;break;}
        if(message->magic!=Magic||message->version!=Version||message->pathLength<3||
           message->pathLength>=MaxPath||message->path[message->pathLength]!=L'\0'||
           !message->requestedSide||message->requestedSide>MaxSide||
           wcslen(message->path)!=message->pathLength){result=6;break;}
#ifdef LUME_ICON_TEST_FIXTURE
        // Compiled only into the disposable fault-injection fixture.
        const std::wstring_view path(message->path);
        if(path==L"C:\\blocked")Sleep(INFINITE);
        if(path==L"C:\\crash"){result=7;break;}
        if(path==L"C:\\malformed"){
            message->width=MaxSide+1;message->height=1;message->result=Result::Ready;
            SetEvent(reply);continue;
        }
#endif
        message->result=Extract(*message)?Result::Ready:Result::Missing;
        if(!SetEvent(reply)){result=8;break;}
    }
    CoUninitialize();UnmapViewOfFile(message);
    CloseHandle(mapping);CloseHandle(request);CloseHandle(reply);CloseHandle(quit);
    return result;
}
