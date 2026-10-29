#include "platform/windows/clock/clock_renderer.h"
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <string>
#include <utility>

namespace lume::platform::windows {
namespace {
template<class T> class Com {
public:
    ~Com() { if (value_) value_->Release(); }
    T* operator->() const { return value_; }
    T* get() const { return value_; }
    T** put() { if (value_) value_->Release(); value_=nullptr; return &value_; }
private:
    T* value_=nullptr;
};

class FontEnumerator final : public IDWriteFontFileEnumerator {
public:
    FontEnumerator(IDWriteFactory* factory, std::vector<std::wstring> files)
        : factory_(factory), files_(std::move(files)) { factory_->AddRef(); }
    ~FontEnumerator() { if (current_) current_->Release(); factory_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result=nullptr;
        if (iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteFontFileEnumerator)) {
            *result=static_cast<IDWriteFontFileEnumerator*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG count=--references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* current) override {
        if (!current) return E_POINTER;
        *current=FALSE;
        if (current_) { current_->Release(); current_=nullptr; }
        if (index_>=files_.size()) return S_OK;
        const HRESULT result=factory_->CreateFontFileReference(files_[index_++].c_str(),nullptr,&current_);
        if (SUCCEEDED(result)) *current=TRUE;
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file) override {
        if (!file) return E_POINTER;
        *file=current_; if (current_) current_->AddRef();
        return current_ ? S_OK : E_FAIL;
    }
private:
    std::atomic<ULONG> references_{1};
    IDWriteFactory* factory_;
    std::vector<std::wstring> files_;
    std::size_t index_=0;
    IDWriteFontFile* current_=nullptr;
};

class FontLoader final : public IDWriteFontCollectionLoader {
public:
    explicit FontLoader(std::vector<std::wstring> files) : files_(std::move(files)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result=nullptr;
        if (iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteFontCollectionLoader)) {
            *result=static_cast<IDWriteFontCollectionLoader*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG count=--references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory, const void*, UINT32,
                                                       IDWriteFontFileEnumerator** result) override {
        if (!result) return E_POINTER;
        *result=new FontEnumerator(factory,files_); return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
    std::vector<std::wstring> files_;
};

const wchar_t* const kFontFiles[]={L"GeosansLight.ttf",L"Ruthligos.ttf",L"Quicksand.otf",
    L"Anurati.otf",L"Allura.otf",L"Futurist Fixed-width.ttf"};
const wchar_t* const kFamilies[]={L"GeosansLight",L"Ruthligos",L"Quicksand",L"Anurati",L"Allura",L"Futurist Fixed-width"};

std::filesystem::path FontDirectory() {
    wchar_t module[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,module,32768);
    const auto adjacent=std::filesystem::path(std::wstring(module,length)).parent_path()/L"ui"/L"assets"/L"fonts";
    if (std::filesystem::is_directory(adjacent)) return adjacent;
    return std::filesystem::current_path()/L"ui"/L"assets"/L"fonts";
}

std::wstring Two(unsigned value) { wchar_t buffer[16]{}; std::swprintf(buffer,16,L"%02u",value); return buffer; }
std::wstring Upper(std::wstring text) {
    for (auto& character:text) if(character>=L'a' && character<=L'z') character-=L'a'-L'A';
    return text;
}
const wchar_t* const kDays[]={L"Sunday",L"Monday",L"Tuesday",L"Wednesday",L"Thursday",L"Friday",L"Saturday"};
const wchar_t* const kMonths[]={L"January",L"February",L"March",L"April",L"May",L"June",L"July",L"August",L"September",L"October",L"November",L"December"};
}

struct ClockRenderer::State {
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Com<IDWriteFactory> write;
    Com<ID2D1Factory> draw;
    Com<IWICImagingFactory> wic;
    Com<IDWriteFontCollection> collection;
    FontLoader* loader=nullptr;
    bool registered=false;
    ~State() {
        collection.put();
        if (registered) write->UnregisterFontCollectionLoader(loader);
        if (loader) loader->Release();
        wic.put(); draw.put(); write.put();
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
};

ClockRenderer::ClockRenderer() = default;
ClockRenderer::~ClockRenderer() = default;

bool ClockRenderer::Initialize() {
    if (state_) return true;
    auto candidate=std::make_unique<State>();
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(candidate->write.put()))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,candidate->draw.put())) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),reinterpret_cast<void**>(candidate->wic.put())))) return false;
    std::vector<std::wstring> files;
    const auto directory=FontDirectory();
    for (const auto* name:kFontFiles) {
        const auto path=directory/name;
        if (!std::filesystem::is_regular_file(path)) return false;
        files.push_back(path.wstring());
    }
    candidate->loader=new FontLoader(std::move(files));
    if (FAILED(candidate->write->RegisterFontCollectionLoader(candidate->loader))) return false;
    candidate->registered=true;
    const int key=1;
    if (FAILED(candidate->write->CreateCustomFontCollection(candidate->loader,&key,sizeof(key),candidate->collection.put()))) return false;
    // DirectWrite otherwise silently substitutes a system face. Require every original family.
    for (const auto* family:kFamilies) {
        UINT32 index=0; BOOL exists=FALSE;
        if (FAILED(candidate->collection->FindFamilyName(family,&index,&exists)) || !exists) return false;
    }
    state_=std::move(candidate);
    return true;
}

bool ClockRenderer::Render(clock::Style style, const clock::Appearance& appearance,
                           const SYSTEMTIME& time, unsigned dpi, ClockBitmap& bitmap) {
    if (!clock::ValidAppearance(appearance) || clock::Index(style)>2 || dpi<48 || dpi>768 ||
        time.wMonth<1 || time.wMonth>12 || time.wDayOfWeek>6 || !Initialize()) return false;
    const float scale=static_cast<float>(appearance.scale)/100.0f;
    const float width=(style==clock::Style::Welfare ? 360.0f : style==clock::Style::Mond ? 720.8f : 520.0f)*scale;
    const float height=(style==clock::Style::Welfare ? 150.0f : style==clock::Style::Mond ? 165.0f : 225.0f)*scale;
    struct Line { Com<IDWriteTextLayout> layout; D2D1_COLOR_F tint{}; float y=0; };
    std::vector<std::unique_ptr<Line>> lines;
    float left=0,top=0,right=0,bottom=0;
    const auto day=std::wstring(kDays[time.wDayOfWeek]);
    const auto month=std::wstring(kMonths[time.wMonth-1]);
    const auto time24=Two(time.wHour)+L":"+Two(time.wMinute);
    bool success=true;
    auto line=[&](const std::wstring& text,const wchar_t* family,float points,float y,float spacing,int color,int alpha) {
        Com<IDWriteTextFormat> format;
        auto prepared=std::make_unique<Line>();
        Com<IDWriteTextLayout1> spaced;
        const int rgb=appearance.customColor ? appearance.color : color;
        const float opacity=static_cast<float>(alpha*(appearance.customColor ? appearance.alpha : 255))/(255.0f*255.0f);
        const auto tint=D2D1::ColorF(static_cast<float>((rgb>>16)&255)/255.0f,static_cast<float>((rgb>>8)&255)/255.0f,
                                    static_cast<float>(rgb&255)/255.0f,opacity);
        if (FAILED(state_->write->CreateTextFormat(family,state_->collection.get(),DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,points*(4.0f/3.0f)*scale,L"en-US",format.put()))) { success=false; return; }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (FAILED(state_->write->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.get(),width,height-y*scale,prepared->layout.put())) ||
            FAILED(prepared->layout->QueryInterface(__uuidof(IDWriteTextLayout1),reinterpret_cast<void**>(spaced.put())))) { success=false; return; }
        if (FAILED(spaced->SetCharacterSpacing(spacing*scale,spacing*scale,0,{0,static_cast<UINT32>(text.size())}))) { success=false; return; }
        DWRITE_OVERHANG_METRICS overflow{};
        if (FAILED(prepared->layout->GetOverhangMetrics(&overflow))) { success=false; return; }
        left=std::max(left,overflow.left); right=std::max(right,overflow.right);
        top=std::max(top,overflow.top-y*scale); bottom=std::max(bottom,overflow.bottom);
        prepared->tint=tint; prepared->y=y*scale;
        lines.push_back(std::move(prepared));
    };
    if (style==clock::Style::Welfare) {
        line(time24,L"GeosansLight",40,50,1,0xffffff,255);
        line(Two(time.wDay)+L" "+month,L"GeosansLight",20,105,1,0xffffff,255);
        line(day,L"Ruthligos",70,0,1,0xffffff,90);
    } else if (style==clock::Style::Mond) {
        line(Upper(day),L"Anurati",40*1.06f,0,10,0xffffff,255);
        line(Upper(Two(time.wDay)+L"  "+month+L",  "+std::to_wstring(time.wYear)+L"."),L"Quicksand",14*1.06f,75*1.06f,0,0xffffff,255);
        line(L"- "+time24+L" -",L"Quicksand",14*1.06f,120*1.06f,0,0xffffff,255);
    } else {
        line(day,L"Allura",480*0.2f,0,0,0xffffff,255);
        line(Upper(Two(time.wDay)+L" "+month+L","+std::to_wstring(time.wYear)+L"."),L"Futurist Fixed-width",60*0.2f,800*0.2f,-1,0xa8b5db,255);
        line(Two(time.wHour%12==0 ? 12 : time.wHour%12)+L":"+Two(time.wMinute)+(time.wHour<12 ? L"|AM" : L"|PM"),
             L"Futurist Fixed-width",80*0.2f,910*0.2f,0,0xffffff,255);
    }
    if (!success) return false;
    // Script faces have long swashes outside their advance widths. Preserve the original
    // meter anchors and expand only the transparent surface, rather than clipping glyphs.
    const float pixelScale=static_cast<float>(dpi)/96.0f;
    bitmap.originX=static_cast<int>(std::ceil(left*pixelScale));
    bitmap.originY=static_cast<int>(std::ceil(top*pixelScale));
    bitmap.width=static_cast<unsigned>(std::ceil((width+right)*pixelScale))+bitmap.originX;
    bitmap.height=static_cast<unsigned>(std::ceil((height+bottom)*pixelScale))+bitmap.originY;
    Com<IWICBitmap> surface;
    Com<ID2D1RenderTarget> target;
    if (FAILED(state_->wic->CreateBitmap(bitmap.width,bitmap.height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,surface.put()))) return false;
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
    if (FAILED(state_->draw->CreateWicBitmapRenderTarget(surface.get(),properties,target.put()))) return false;
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0,0,0,0));
    for (const auto& prepared:lines) {
        Com<ID2D1SolidColorBrush> brush;
        if (FAILED(target->CreateSolidColorBrush(prepared->tint,brush.put()))) { success=false; break; }
        target->DrawTextLayout(D2D1::Point2F(static_cast<float>(bitmap.originX)/pixelScale,
            prepared->y+static_cast<float>(bitmap.originY)/pixelScale),prepared->layout.get(),brush.get(),D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    if (FAILED(target->EndDraw()) || !success) return false;
    bitmap.pixels.resize(static_cast<std::size_t>(bitmap.width)*bitmap.height);
    return SUCCEEDED(surface->CopyPixels(nullptr,bitmap.width*4,static_cast<UINT>(bitmap.pixels.size()*4),
        reinterpret_cast<BYTE*>(bitmap.pixels.data())));
}
} // namespace lume::platform::windows
