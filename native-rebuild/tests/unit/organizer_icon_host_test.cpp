#include "platform/windows/organizer/organizer_icon_client.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/shell_open.h"
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
namespace {
using namespace lume::platform::windows;
std::wstring expectedOpenPath;
bool shellOpenSucceeds=true;
BOOL WINAPI VerifyShellOpen(SHELLEXECUTEINFOW* request) {
    APTTYPE type{};APTTYPEQUALIFIER qualifier{};
    if(FAILED(CoGetApartmentType(&type,&qualifier))||(type!=APTTYPE_STA&&type!=APTTYPE_MAINSTA))return FALSE;
    if(!request||request->cbSize!=sizeof(*request)||request->lpFile!=expectedOpenPath||
       std::wstring(request->lpVerb)!=L"open"||!(request->fMask&SEE_MASK_NOASYNC)||request->nShow!=SW_SHOWNORMAL)return FALSE;
    return shellOpenSucceeds?TRUE:FALSE;
}
void Check(bool value,const char* message){
    if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
template<class F>bool Until(F&& condition) {
    const auto end=GetTickCount64()+4000;
    while(!condition()&&GetTickCount64()<end)Sleep(2);
    return condition();
}
bool Stopped(unsigned pid) {
    const auto process=OpenProcess(SYNCHRONIZE,FALSE,pid);
    if(!process)return true;
    const auto status=WaitForSingleObject(process,1000);CloseHandle(process);
    return status==WAIT_OBJECT_0;
}
template<class T>struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
    T** Put(){return &value;}
    T* operator->()const{return value;}
};
struct IconFiles {
    explicit IconFiles(const std::filesystem::path& directory):directory(directory) {
        std::filesystem::create_directories(directory);
    }
    ~IconFiles(){std::error_code error;std::filesystem::remove_all(directory,error);}
    std::filesystem::path directory;
};
void Word(std::ostream& out,unsigned value) {
    out.put(static_cast<char>(value&255));out.put(static_cast<char>((value>>8)&255));
}
void Dword(std::ostream& out,unsigned value) {
    Word(out,value&65535);Word(out,value>>16);
}
void WriteIcon(const std::filesystem::path& path) {
    std::ofstream out(path,std::ios::binary);Check(out.good(),"create isolated multi-resolution icon");
    constexpr unsigned sides[]{32,64,128,256};
    Word(out,0);Word(out,1);Word(out,4);
    unsigned offset=6+4*16;
    for(const auto side:sides) {
        const unsigned bytes=40+side*side*4+side*((side+31)/32)*4;
        out.put(static_cast<char>(side));out.put(static_cast<char>(side));Word(out,0);
        Word(out,1);Word(out,32);Dword(out,bytes);Dword(out,offset);offset+=bytes;
    }
    for(const auto side:sides) {
        const unsigned maskBytes=side*((side+31)/32)*4;
        Dword(out,40);Dword(out,side);Dword(out,side*2);Word(out,1);Word(out,32);
        Dword(out,BI_RGB);Dword(out,side*side*4+maskBytes);
        for(unsigned field=0;field<4;++field)Dword(out,0);
        // The low-resolution resource is deliberately different. Upscaling it
        // cannot satisfy the high-resolution color/shape assertions below.
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
            const bool transparent=x<side/8||y<side/8||x>=side*7/8||y>=side*7/8;
            Dword(out,transparent?0:(side==32?0xffff3322u:0xff22bb66u));
        }
        for(unsigned byte=0;byte<maskBytes;++byte)out.put('\0');
    }
    Check(out.good(),"write isolated multi-resolution icon");
}
void WriteShortcut(const std::filesystem::path& path,const std::wstring& target,
    const std::wstring& icon) {
    Com<IShellLinkW> shortcut;Com<IPersistFile> file;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(shortcut.Put()))),"create isolated shortcut");
    Check(SUCCEEDED(shortcut->SetPath(target.c_str()))&&
        SUCCEEDED(shortcut->SetIconLocation(icon.c_str(),0))&&
        SUCCEEDED(shortcut->QueryInterface(IID_PPV_ARGS(file.Put())))&&
        SUCCEEDED(file->Save(path.c_str(),TRUE)),"save shortcut with custom icon");
}
void VerifyShortcutPixels(const std::wstring& target,const std::wstring& fixture,const std::atomic_bool& cancelled) {
    IconFiles files(std::filesystem::temp_directory_path()/
        (L"lume-organizer-icons-"+std::to_wstring(GetCurrentProcessId())));
    const auto iconPath=files.directory/L"custom.ico",shortcutPath=files.directory/L"custom.lnk";
    WriteIcon(iconPath);WriteShortcut(shortcutPath,target,iconPath.wstring());
    OrganizerIconExtractor extractor;
    const auto source=extractor.Read(iconPath.wstring(),cancelled);
    Check(source&&source->width>=64&&source->height>=64,"read high-resolution icon instead of scaling a 32px image");
    const auto shortcut=extractor.Read(shortcutPath.wstring(),cancelled);
    Check(shortcut&&shortcut->width==source->width&&shortcut->height==source->height,
        "shortcut keeps the custom high-resolution icon");
    Check(shortcut->pixels==source->pixels,"shortcut icon has no shell shortcut-arrow overlay");
    unsigned green=0,red=0;
    for(const auto color:shortcut->pixels) {
        if(((color>>8)&255)>((color>>16)&255)*2)++green;
        if(((color>>16)&255)>((color>>8)&255)*2)++red;
    }
    Check(green>shortcut->pixels.size()/3&&red==0,"shortcut uses the high-resolution resource, not target or enlarged small icon");
    for(const unsigned requested:{88u,132u}) {
        const auto scaledSource=extractor.Read(iconPath.wstring(),cancelled,requested);
        const auto scaledShortcut=extractor.Read(shortcutPath.wstring(),cancelled,requested);
        Check(scaledSource&&scaledShortcut&&scaledShortcut->width>=requested&&scaledShortcut->height>=requested,
            "requested physical icon pixels support 192 and 288 DPI without canvas upscaling");
        Check(scaledShortcut->pixels==scaledSource->pixels,"DPI-sized shortcut preserves custom pixels without arrows");
    }
    const auto customFallback=files.directory/L"shell-unavailable-custom.lnk";
    const auto targetFallback=files.directory/L"shell-unavailable-target.lnk";
    WriteShortcut(customFallback,target,iconPath.wstring());
    WriteShortcut(targetFallback,iconPath.wstring(),L"");
    OrganizerIconExtractor unavailableShell(fixture);
    const auto custom=unavailableShell.Read(customFallback.wstring(),cancelled,88);
    const auto fallbackTarget=unavailableShell.Read(targetFallback.wstring(),cancelled,88);
    const auto expected=extractor.Read(iconPath.wstring(),cancelled,88);
    Check(custom&&expected&&custom->pixels==expected->pixels,
        "unavailable Shell factory falls back to the shortcut custom icon without an arrow");
    Check(fallbackTarget&&fallbackTarget->pixels==expected->pixels,
        "shortcut without an explicit icon falls back to its target without launching or resolving it");
    const auto folder=extractor.Read(files.directory.wstring(),cancelled,88);
    if(!folder) {
        Com<IShellItemImageFactory> image;HBITMAP bitmap=nullptr;
        const auto created=SHCreateItemFromParsingName(files.directory.c_str(),nullptr,IID_PPV_ARGS(image.Put()));
        const auto read=SUCCEEDED(created)?image->GetImage({88,88},SIIGBF_RESIZETOFIT,&bitmap):created;
        BITMAP description{};if(bitmap)GetObjectW(bitmap,sizeof(description),&description);
        std::cout<<"folder diagnostic: create="<<std::hex<<created<<" read="<<read<<std::dec
            <<" size="<<description.bmWidth<<'x'<<description.bmHeight<<'\n';
        if(bitmap&&description.bmWidth>0&&description.bmHeight>0){
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(description.bmWidth)*description.bmHeight);
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=description.bmWidth;info.bmiHeader.biHeight=-description.bmHeight;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            HDC dc=CreateCompatibleDC(nullptr);
            const auto rows=GetDIBits(dc,bitmap,0,static_cast<UINT>(description.bmHeight),pixels.data(),&info,DIB_RGB_COLORS);
            unsigned ink=0,invalid=0,transparentRgb=0;
            for(const auto color:pixels){
                const auto alpha=color>>24;if(alpha)++ink;else if(color&0xffffff)++transparentRgb;
                if(((color>>16)&255)>alpha||((color>>8)&255)>alpha||(color&255)>alpha)++invalid;
            }
            std::cout<<"folder pixels: rows="<<rows<<" ink="<<ink<<" invalid="<<invalid<<" transparentRgb="<<transparentRgb<<'\n';
            if(dc)DeleteDC(dc);
        }
        if(bitmap)DeleteObject(bitmap);
    }
    Check(folder&&!folder->pixels.empty(),"folders still use their Shell image");
}
int ProbeIcons(const std::filesystem::path& output) {
    int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)return 2;
    unsigned pixelSize=64;int first=1;
    if(count>1&&wcsncmp(args[1],L"--pixels=",9)==0) {
        wchar_t* end=nullptr;const auto parsed=wcstoul(args[1]+9,&end,10);
        if(!end||*end||!parsed||parsed>256){LocalFree(args);return 2;}
        pixelSize=static_cast<unsigned>(parsed);first=2;
    }
    std::atomic_bool cancelled{false};int failed=0;
    for(int i=first;i<count;++i) {
        const auto begin=GetTickCount64();
        const auto icon=ReadOrganizerIcon(args[i],cancelled,pixelSize);
        const auto number=i-first+1;
        std::cout<<"icon-probe-"<<number<<": "<<(icon?"ready":"missing")<<" in "<<GetTickCount64()-begin<<"ms";
        if(!icon){std::cout<<'\n';++failed;continue;}
        std::cout<<", "<<icon->width<<"x"<<icon->height<<'\n';
        const auto path=output/(L"icon-probe-"+std::to_wstring(number)+L".ppm");
        std::ofstream image(path,std::ios::binary);
        image<<"P6\n"<<icon->width<<' '<<icon->height<<"\n255\n";
        for(const auto color:icon->pixels)for(const unsigned shift:{16u,8u,0u})
            image.put(static_cast<char>(((color>>shift)&255)+((255-(color>>24))*48/255)));
    }
    LocalFree(args);return failed?1:0;
}
}
int main(int argc,char**) {
    // A fresh helper starts without COM. Verify initialization before handing
    // a folder/shortcut to the Shell, and balanced cleanup on both outcomes.
    std::thread openThread([]{
        for(const auto* path:{L"C:\\",L"D:\\Folder with spaces\\",L"C:\\资料\\目录",L"C:\\Folder shortcut.lnk"}){
            expectedOpenPath=path;shellOpenSucceeds=true;
            Check(OpenShellTarget(path,VerifyShellOpen),"folder and shortcut opens execute inside an STA with the exact path");
            APTTYPE type{};APTTYPEQUALIFIER qualifier{};
            Check(CoGetApartmentType(&type,&qualifier)==CO_E_NOTINITIALIZED,"helper releases apartment after successful open");
            shellOpenSucceeds=false;
            Check(!OpenShellTarget(path,VerifyShellOpen),"Shell open failures are not reported successful");
            Check(CoGetApartmentType(&type,&qualifier)==CO_E_NOTINITIALIZED,"helper releases apartment after failed open");
        }
        Check(!OpenShellTarget(L"",VerifyShellOpen),"empty Shell target rejected");
    });
    openThread.join();
    using namespace lume::platform::windows;
    wchar_t executable[32768]{},windows[32768]{};
    Check(GetModuleFileNameW(nullptr,executable,32768)>0&&GetWindowsDirectoryW(windows,32768)>0,"fixture paths");
    const auto root=std::filesystem::path(executable).parent_path();
    if(argc>1)return ProbeIcons(root);
    const auto fixture=(root/L"lume-organizer-icon-fixture.exe").wstring();
    const auto actual=std::wstring(windows)+L"\\explorer.exe";
    std::atomic_bool cancelled{false};
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM for isolated shortcut fixture");
    VerifyShortcutPixels(actual,fixture,cancelled);
    CoUninitialize();
    Check(OrganizerIconExtractor::ActiveHosts()==0,"disabled extraction has no child");
    {
        OrganizerIconExtractor extractor;
        const auto icon=extractor.Read(actual,cancelled);
        Check(icon&&!icon->pixels.empty(),"production helper reads a real Shell icon");
        const auto pid=extractor.ProcessId();
        Check(pid!=0&&pid!=GetCurrentProcessId()&&OrganizerIconExtractor::ActiveHosts()==1,"extraction runs outside application");
        Check(extractor.Read(actual,cancelled)!=nullptr&&extractor.ProcessId()==pid,"finite batch reuses one helper");
        extractor.Stop();
        Check(Stopped(pid)&&OrganizerIconExtractor::ActiveHosts()==0,"batch end reaps actual child process");
    }
    {
        OrganizerIconExtractor extractor(fixture,10000);
        std::shared_ptr<const WidgetBitmap> result;
        std::thread worker([&]{result=extractor.Read(L"C:\\blocked",cancelled);});
        Check(Until([]{return OrganizerIconExtractor::ActiveHosts()==1;}),"uncooperative Shell fixture started");
        Sleep(80);
        const auto child=OpenProcess(SYNCHRONIZE,FALSE,extractor.ProcessId());
        Check(child!=nullptr,"hold actual hung child process handle");
        const auto begin=GetTickCount64();cancelled=true;worker.join();
        Check(!result&&GetTickCount64()-begin<1000&&OrganizerIconExtractor::ActiveHosts()==0,
              "cancellation terminates uncooperative helper without waiting for Shell");
        Check(WaitForSingleObject(child,0)==WAIT_OBJECT_0,"cancelled child really exited");
        CloseHandle(child);
        cancelled=false;
    }
    for(const auto* path:{L"C:\\blocked",L"C:\\crash",L"C:\\malformed"}) {
        OrganizerIconExtractor extractor(fixture,150);
        const auto begin=GetTickCount64();
        Check(!extractor.Read(path,cancelled),"timeout, crash or malformed pixels fail closed");
        Check(GetTickCount64()-begin<2000&&OrganizerIconExtractor::ActiveHosts()==0,
              "failed decoder releases helper and IPC handles");
        Check(extractor.Read(actual,cancelled)!=nullptr,"failed extraction recovers with a fresh helper");
    }
    const auto batch=[&]{
        OrganizerIconCache cache;
        Check(cache.SetPaths({actual},{}),"production cache starts batch");
        Check(Until([&]{return cache.Find(actual)!=nullptr&&OrganizerIconCache::ActiveWorkers()==0&&
            OrganizerIconExtractor::ActiveHosts()==0;}),"completed cache keeps pixels without idle child or worker");
        cache.Stop();
        Check(OrganizerIconCache::ActiveWorkers()==0&&OrganizerIconExtractor::ActiveHosts()==0,"closed cache owns no extraction resources");
    };
    // The cold batch measured 72 -> 74 handles, then stayed at 74 for twelve
    // cycles. Compare repeated identical loads after the same-load warm-up.
    for(unsigned round=0;round<4;++round)batch();
    DWORD baseline=0,after=0;
    Check(GetProcessHandleCount(GetCurrentProcess(),&baseline)!=FALSE,"same-load handle baseline");
    for(unsigned round=0;round<12;++round)batch();
    Check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"final handle sample");
    std::cout<<"helper handles before="<<baseline<<" after="<<after<<'\n';
    Check(after<=baseline+1,
          "repeated default extraction releases IPC and process handles");
    Check(OrganizerIconExtractor::PendingCleanup()==0,"no unobserved pending kernel cleanup");
    std::cout<<"organizer real helper, uncooperative cancellation, timeout/crash recovery and 12-cycle cleanup passed\n";
}
