#include "platform/windows/screenshot/screenshot_image.h"
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace lume::platform::windows::capture;
namespace {
void Expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Image Sample(){return {3,2,{0xffff0000u,0xff00ff00u,0xff0000ffu,0xff112233u,0xff445566u,0xff778899u}};}
void Same(const Image& actual,const Image& expected,const char* message){
    Expect(actual.width==expected.width&&actual.height==expected.height&&actual.pixels==expected.pixels,message);
}
void Crops(){
    const auto source=Sample();
    Same(Crop(source,{0,0,3,2}),source,"full crop preserves dimensions, channel order and row order");
    Same(Crop(source,{1,0,3,2}),{2,2,{0xff00ff00u,0xff0000ffu,0xff445566u,0xff778899u}},"crop copies both rows");
    Same(Crop(source,{-4,-3,2,1}),{2,1,{0xffff0000u,0xff00ff00u}},"negative crop clamps to top-left");
    Same(Crop(source,{2,1,99,99}),{1,1,{0xff778899u}},"out-of-range crop clamps to bottom-right, including one pixel");
    Same(Crop(source,{(std::numeric_limits<int>::min)(),(std::numeric_limits<int>::min)(),
                      (std::numeric_limits<int>::max)(),(std::numeric_limits<int>::max)()}),source,
         "extreme bounds clamp before subtraction");
    const lume::screenshot::Rect emptyRects[]={{-4,-3,-1,-1},{3,0,9,2},{0,2,3,9},{1,0,1,2},{2,1,1,0}};
    for(const auto rect:emptyRects)
        Same(Crop(source,rect),{},"outside, empty and reversed crops are empty");
    for(const Image& invalid:std::vector<Image>{{},{0,2,{}},{-1,2,{}},{3,2,{1}},
                                               {(std::numeric_limits<int>::max)(),2,{}},{1,1,{1,2}}})
        Same(Crop(invalid,{0,0,3,2}),{},"invalid dimensions and buffer lengths are rejected");
    Same(source,Sample(),"crop never changes the input");
    const Image alpha{1,1,{0x80123456u}};
    Same(Crop(alpha,{0,0,1,1}),alpha,"crop preserves source alpha");
}
void SaveErrors(){
    SetLastError(ERROR_CANCELLED);
    Expect(!SaveImage(nullptr,Sample()),"save rejects missing modal owner without showing a dialog");
    Expect(GetLastError()==ERROR_GEN_FAILURE,"invalid owner reports failure, not stale cancellation");
    SetLastError(ERROR_SUCCESS);
    Expect(!SaveImage(GetDesktopWindow(),{}),"save rejects invalid image without showing a dialog");
    Expect(GetLastError()==ERROR_GEN_FAILURE,"invalid image reports save failure");
}
void ClipboardRoundTrip(){
    const auto expected=Sample();
    Expect(CopyImage(GetDesktopWindow(),expected),"copy publishes a DIB to the clipboard");
    Expect(OpenClipboard(GetDesktopWindow())!=FALSE,"open clipboard after copy");
    const auto handle=GetClipboardData(CF_DIB);Expect(handle!=nullptr,"clipboard exposes CF_DIB");
    const auto* bytes=static_cast<const BYTE*>(GlobalLock(handle));Expect(bytes!=nullptr,"lock clipboard DIB");
    const auto* header=reinterpret_cast<const BITMAPINFOHEADER*>(bytes);
    Expect(header->biWidth==expected.width&&header->biHeight==expected.height&&header->biBitCount==32,"clipboard DIB uses standard bottom-up dimensions");
    const auto* pixels=reinterpret_cast<const std::uint32_t*>(bytes+header->biSize);
    Expect(pixels[0]==expected.pixels[3]&&pixels[expected.width]==expected.pixels[0],"clipboard DIB preserves row order for standard consumers");
    GlobalUnlock(handle);CloseClipboard();
}
struct GdiSession {
    ULONG_PTR token=0;
    GdiSession(){Gdiplus::GdiplusStartupInput input;Expect(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"GDI+ startup");}
    ~GdiSession(){Gdiplus::GdiplusShutdown(token);}
};
struct TestDirectory {
    std::filesystem::path path;
    TestDirectory(){
        GUID guid{};Expect(SUCCEEDED(CoCreateGuid(&guid)),"unique test directory ID");
        wchar_t name[40]{};Expect(StringFromGUID2(guid,name,40)>0,"format test directory ID");
        path=std::filesystem::temp_directory_path()/(std::wstring(L"lume-screenshot-image-test-")+name);
        Expect(std::filesystem::create_directory(path),"create exclusively owned test directory");
    }
    ~TestDirectory(){
        // This exact directory was created exclusively by this test; never recurse elsewhere.
        std::error_code error;std::filesystem::remove_all(path,error);
    }
};
struct FileLock {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit FileLock(const std::filesystem::path& path){
        handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Expect(handle!=INVALID_HANDLE_VALUE,"lock test destination against replacement");
    }
    ~FileLock(){CloseHandle(handle);}
};
std::string Bytes(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);Expect(input.good(),"open test PNG");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void RoundTrip(const std::filesystem::path& path,const Image& expected){
    Gdiplus::Bitmap decoded(path.c_str());
    Expect(decoded.GetLastStatus()==Gdiplus::Ok,"decode written PNG");
    Expect(decoded.GetWidth()==static_cast<UINT>(expected.width)&&decoded.GetHeight()==static_cast<UINT>(expected.height),"PNG dimensions");
    for(int y=0;y<expected.height;++y)for(int x=0;x<expected.width;++x){
        Gdiplus::Color color;
        Expect(decoded.GetPixel(x,y,&color)==Gdiplus::Ok,"read PNG pixel");
        Expect(color.GetValue()==expected.pixels[static_cast<std::size_t>(y)*static_cast<std::size_t>(expected.width)+static_cast<std::size_t>(x)],
               "PNG round-trip preserves BGRA colors and row order");
    }
}
void Pngs(){
    TestDirectory directory;
    const auto file=directory.path/L"roundtrip-\u622a\u56fe.png";
    const auto original=Sample();
    {
        std::ofstream existing(file,std::ios::binary);existing<<"original";
        Expect(existing.good(),"write test-owned original destination");
    }
    GdiSession gdiplus;
    Expect(WritePng(original,file.wstring()),"synthetic PNG save succeeds");
    RoundTrip(file,original);
    const auto bytes=Bytes(file);
    Expect(bytes.size()>8&&bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0,"file has PNG signature");
    Expect(!WritePng({},file.wstring())&&Bytes(file)==bytes,"invalid image preserves existing file");
    Expect(!WritePng({3,2,{1}},file.wstring())&&Bytes(file)==bytes,"malformed buffer preserves existing file");
    Expect(!WritePng(original,L""),"empty path is rejected");
    Expect(!WritePng(original,file.wstring()+std::wstring(1,L'\0')+L"suffix"),"embedded NUL is rejected");
    Expect(!WritePng(original,(directory.path/L"missing"/L"image.png").wstring()),"missing parent fails without creating directories");
    Expect(!WritePng(original,directory.path.wstring()),"directory destination cannot be replaced");
    const Image replacement{1,1,{0xffaabbccu}};
    {
        FileLock lock(file);
        Expect(!WritePng(replacement,file.wstring()),"locked destination replacement fails");
        Expect(Bytes(file)==bytes,"failed replacement preserves original bytes");
    }
    Expect(std::distance(std::filesystem::directory_iterator(directory.path),std::filesystem::directory_iterator{})==1,
           "failed writes leave no temporary files");
    Expect(WritePng(replacement,file.wstring()),"unlocked destination can be replaced");
    RoundTrip(file,replacement);
    const Image alpha{2,1,{0x80123456u,0x40102030u}};
    Expect(WritePng(alpha,file.wstring()),"PNG supports synthetic alpha");
    RoundTrip(file,alpha);
    Expect(std::distance(std::filesystem::directory_iterator(directory.path),std::filesystem::directory_iterator{})==1,
           "successful replacement leaves only destination");
    Same(original,Sample(),"PNG encoding never changes the source");
}
}
int main(int argc,char** argv){
    try {
        const std::string group=argc>1?argv[1]:"all";
        if(group!="png"){Crops();SaveErrors();ClipboardRoundTrip();}
        if(group!="crop")Pngs();
        std::cout<<"PASS screenshot image "<<group<<" (synthetic only; no capture, dialog or clipboard)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
