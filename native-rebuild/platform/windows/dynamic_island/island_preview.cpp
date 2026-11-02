#include "platform/windows/dynamic_island/island_preview.h"
#include "material-shader-bytecode.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace lume::platform::windows {
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned Width=960,Height=300;
struct Constants {
    float surfaceOrigin[2]{},surfaceSize[2]{Width,Height},outputOrigin[2]{},outputSize[2]{Width,Height};
    float lensOrigin[2]{},lensSize[2]{},effect[4]{.69f,.18f,1.1f,1};
    float sampleBoundsMin[2]{},sampleBoundsMax[2]{Width-1,Height-1};
    float interaction[4]{},motion[4]{},readability[4]{1,1,0,0},optical[4]{};
};
static_assert(sizeof(Constants)==144);
WidgetBitmap Background(const std::wstring& path) {
    WidgetBitmap result{Width,Height,std::vector<std::uint32_t>(Width*Height,0xff242f2e)};
    if(path.empty())return result;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> clip;ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> convert;
    UINT w=0,h=0;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||
       FAILED(decoder->GetFrame(0,&frame))||FAILED(frame->GetSize(&w,&h))||!w||!h||w>16384||h>16384)return result;
    const double factor=std::max(double(Width)/w,double(Height)/h);
    const int cropW=std::min(static_cast<int>(w),static_cast<int>(std::ceil(Width/factor)));
    const int cropH=std::min(static_cast<int>(h),static_cast<int>(std::ceil(Height/factor)));
    const WICRect rect{(static_cast<int>(w)-cropW)/2,static_cast<int>((h-cropH)*.45),cropW,cropH};
    if(FAILED(factory->CreateBitmapClipper(&clip))||FAILED(clip->Initialize(frame.Get(),&rect))||
       FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(clip.Get(),Width,Height,WICBitmapInterpolationModeFant))||
       FAILED(factory->CreateFormatConverter(&convert))||FAILED(convert->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGRA,
          WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return result;
    if(FAILED(convert->CopyPixels(nullptr,Width*4,Width*Height*4,reinterpret_cast<BYTE*>(result.pixels.data()))))return {};
    return result;
}
}
struct IslandPreviewRenderer::Impl {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> liquid,frosted;
    ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11Texture2D> target,staging,source;ComPtr<ID3D11RenderTargetView> view;ComPtr<ID3D11ShaderResourceView> sourceView;
    std::wstring path;std::filesystem::file_time_type stamp{};WidgetBitmap background;
    bool Ready() {
        if(vertex)return true;
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr))return false;
        namespace bytes=lume_material_bytecode;
        if(FAILED(device->CreateVertexShader(bytes::vertex,sizeof(bytes::vertex),nullptr,&vertex))||
           FAILED(device->CreatePixelShader(bytes::liquid,sizeof(bytes::liquid),nullptr,&liquid))||
           FAILED(device->CreatePixelShader(bytes::frosted,sizeof(bytes::frosted),nullptr,&frosted))) {vertex.Reset();return false;}
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        if(FAILED(device->CreateBuffer(&cb,nullptr,&constants))||FAILED(device->CreateSamplerState(&sd,&sampler))||
           FAILED(device->CreateRasterizerState(&rd,&rasterizer))) {vertex.Reset();return false;}
        D3D11_TEXTURE2D_DESC td{};td.Width=Width;td.Height=Height;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(device->CreateTexture2D(&td,nullptr,&target))||FAILED(device->CreateRenderTargetView(target.Get(),nullptr,&view))) {vertex.Reset();return false;}
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateTexture2D(&td,nullptr,&staging))) {vertex.Reset();return false;}
        return true;
    }
    bool Backdrop(const std::wstring& value) {
        std::error_code ec;const auto modified=value.empty()?std::filesystem::file_time_type{}:std::filesystem::last_write_time(value,ec);
        if(!background.pixels.empty()&&path==value&&stamp==modified)return true;
        background=Background(value);path=value;stamp=modified;
        sourceView.Reset();source.Reset();return !background.pixels.empty();
    }
    WidgetBitmap Material(const IslandPreviewFrame& fg,const IslandDisplay& display,unsigned left,unsigned top) {
        if(!Ready())return {};
        if(!source) {
            D3D11_TEXTURE2D_DESC td{};td.Width=Width;td.Height=Height;td.MipLevels=td.ArraySize=1;
            td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{background.pixels.data(),Width*4,0};
            if(FAILED(device->CreateTexture2D(&td,&data,&source))||FAILED(device->CreateShaderResourceView(source.Get(),nullptr,&sourceView)))return {};
        }
        Constants c;c.lensOrigin[0]=static_cast<float>(left);c.lensOrigin[1]=static_cast<float>(top);
        c.lensSize[0]=static_cast<float>(fg.width);c.lensSize[1]=static_cast<float>(fg.height);
        c.optical[1]=-(1+fg.shapeMix);c.optical[2]=1;c.optical[3]=fg.cornerRadius;
        if(display.material==IslandMaterial::Frosted){c.effect[0]=-1;c.effect[2]=-fg.cornerRadius;c.readability[0]=c.readability[1]=0;}
        context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);
        context->PSSetShader(display.material==IslandMaterial::Frosted?frosted.Get():liquid.Get(),nullptr,0);
        context->PSSetConstantBuffers(0,1,constants.GetAddressOf());context->PSSetSamplers(0,1,sampler.GetAddressOf());
        context->PSSetShaderResources(0,1,sourceView.GetAddressOf());context->RSSetState(rasterizer.Get());
        const D3D11_VIEWPORT vp{0,0,float(Width),float(Height),0,1};context->RSSetViewports(1,&vp);
        context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr);context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        const float clear[4]{};context->ClearRenderTargetView(view.Get(),clear);context->Draw(3,0);
        ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);context->OMSetRenderTargets(0,nullptr,nullptr);
        context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return {};
        WidgetBitmap output{Width,Height,std::vector<std::uint32_t>(Width*Height)};
        for(unsigned y=0;y<Height;++y)std::memcpy(output.pixels.data()+y*Width,static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch,Width*4);
        context->Unmap(staging.Get(),0);return output;
    }
};
IslandPreviewRenderer::IslandPreviewRenderer():impl_(std::make_unique<Impl>()){}
IslandPreviewRenderer::~IslandPreviewRenderer()=default;
WidgetBitmap IslandPreviewRenderer::Render(const IslandPreviewFrame& fg,const IslandDisplay& display,const std::wstring& wallpaper) {
    if(!fg.width||!fg.height||fg.width>Width||fg.height>Height||fg.pixels.pixels.size()!=std::size_t(fg.width)*fg.height||!impl_->Backdrop(wallpaper))return {};
    auto output=impl_->background;
    const unsigned left=(Width-fg.width)/2,top=static_cast<unsigned>(std::lround(fg.topOffset));
    const auto over=[](std::uint32_t src,std::uint32_t dst){
        const auto inverse=255-(src>>24);std::uint32_t p=0xff000000;
        for(unsigned shift:{0u,8u,16u})p|=std::min(255u,((src>>shift)&255)+(((dst>>shift)&255)*inverse+127)/255)<<shift;
        return p;
    };
    if(display.material!=IslandMaterial::None) {
        const auto glass=impl_->Material(fg,display,left,top);if(glass.pixels.empty())return {};
        for(std::size_t i=0;i<output.pixels.size();++i)output.pixels[i]=over(glass.pixels[i],output.pixels[i]);
    }
    for(unsigned y=0;y<fg.height&&y+top<Height;++y)for(unsigned x=0;x<fg.width;++x){
        const auto at=std::size_t(y+top)*Width+left+x;
        output.pixels[at]=over(fg.pixels.pixels[std::size_t(y)*fg.width+x],output.pixels[at]);
    }
    return output;
}
std::string IslandPreviewRenderer::PngDataUrl(const WidgetBitmap& image) {
    if(!image.width||!image.height||image.width>Width||image.height>Height||image.pixels.size()!=std::size_t(image.width)*image.height)return {};
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IStream> stream;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateBitmapFromMemory(image.width,image.height,GUID_WICPixelFormat32bppPBGRA,image.width*4,
          static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())),&bitmap))||
       FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||
       FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,nullptr))||
       FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(image.width,image.height)))return {};
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(FAILED(frame->SetPixelFormat(&format))||FAILED(frame->WriteSource(bitmap.Get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit()))return {};
    STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||!stat.cbSize.QuadPart||stat.cbSize.QuadPart>2*1024*1024)return {};
    LARGE_INTEGER start{};ULONG read=0;std::vector<BYTE> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    if(FAILED(stream->Seek(start,STREAM_SEEK_SET,nullptr))||FAILED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read))||read!=bytes.size())return {};
    DWORD count=0;if(!CryptBinaryToStringA(bytes.data(),read,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&count))return {};
    std::string text(count,'\0');if(!CryptBinaryToStringA(bytes.data(),read,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,text.data(),&count))return {};
    if(!text.empty()&&text.back()=='\0')text.pop_back();return "data:image/png;base64,"+text;
}
std::string IslandPreviewRenderer::JpegDataUrl(const WidgetBitmap& image) {
    if(!image.width||!image.height||image.width>Width||image.height>Height||image.pixels.size()!=std::size_t(image.width)*image.height)return {};
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IStream> stream;ComPtr<IPropertyBag2> options;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateBitmapFromMemory(image.width,image.height,GUID_WICPixelFormat32bppBGR,image.width*4,
          static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())),&bitmap))||
       FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder))||
       FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,&options)))return {};
    PROPBAG2 property{};property.pstrName=const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT quality{};quality.vt=VT_R4;quality.fltVal=.84f;
    if(options)options->Write(1,&property,&quality);
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    if(FAILED(frame->Initialize(options.Get()))||FAILED(frame->SetSize(image.width,image.height))||
       FAILED(frame->SetPixelFormat(&format))||FAILED(frame->WriteSource(bitmap.Get(),nullptr))||
       FAILED(frame->Commit())||FAILED(encoder->Commit()))return {};
    STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||!stat.cbSize.QuadPart||stat.cbSize.QuadPart>1024*1024)return {};
    LARGE_INTEGER start{};ULONG read=0;std::vector<BYTE> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    if(FAILED(stream->Seek(start,STREAM_SEEK_SET,nullptr))||FAILED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read))||read!=bytes.size())return {};
    DWORD count=0;if(!CryptBinaryToStringA(bytes.data(),read,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&count))return {};
    std::string text(count,'\0');if(!CryptBinaryToStringA(bytes.data(),read,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,text.data(),&count))return {};
    if(!text.empty()&&text.back()=='\0')text.pop_back();return "data:image/jpeg;base64,"+text;
}
bool IslandPreviewRenderer::WritePng(const WidgetBitmap& image,const std::wstring& path) {
    const auto url=PngDataUrl(image);if(url.empty())return false;
    const auto comma=url.find(',');if(comma==std::string::npos)return false;
    DWORD bytes=0;
    if(!CryptStringToBinaryA(url.data()+comma+1,static_cast<DWORD>(url.size()-comma-1),CRYPT_STRING_BASE64,nullptr,&bytes,nullptr,nullptr)||!bytes)return false;
    std::vector<BYTE> data(bytes);
    if(!CryptStringToBinaryA(url.data()+comma+1,static_cast<DWORD>(url.size()-comma-1),CRYPT_STRING_BASE64,data.data(),&bytes,nullptr,nullptr))return false;
    HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(output,data.data(),bytes,&written,nullptr)&&written==bytes;
    CloseHandle(output);return ok;
}
}
