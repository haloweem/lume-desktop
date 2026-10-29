#include "platform/windows/material/materials/liquid-glass.h"
#include "platform/windows/material/materials/frosted-glass.h"
#include "material-shader-bytecode.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
    Com()=default;
    Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;
    T** Out(){return &value;}
    T* operator->() const{return value;}
};
void Check(HRESULT result,const char* operation){
    if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(static_cast<unsigned long>(result)));
}
int failures=0;
void Expect(bool condition,const std::string& description){
    if(!condition){++failures;std::cerr<<"FAIL: "<<description<<'\n';}
}
struct Constants {
    float surfaceOrigin[2]{},surfaceSize[2]{},outputOrigin[2]{},outputSize[2]{};
    float lensOrigin[2]{},lensSize[2]{},effect[4]{};
    float sampleBoundsMin[2]{},sampleBoundsMax[2]{};
    float interaction[4]{},motion[4]{},readability[4]{},optical[4]{};
};
static_assert(sizeof(Constants)==144,"HLSL cbuffer packing");
struct Pixel {std::uint8_t blue,green,red,alpha;};
static_assert(sizeof(Pixel)==4);
// Fit the complete 320-DIP annulus at 192 DPI, including its origin and AA fringe.
constexpr UINT kSize=704;
constexpr int kOrigin=32;
using Image=std::vector<Pixel>;
const Pixel& At(const Image& image,int x,int y){return image[static_cast<std::size_t>(y)*kSize+static_cast<std::size_t>(x)];}
Image Solid(std::uint8_t gray){return Image(kSize*kSize,Pixel{gray,gray,gray,255});}
Constants Parameters(unsigned profile,float dpi=1.0f,float radius=0.0f){
    Constants c;
    c.surfaceSize[0]=c.surfaceSize[1]=static_cast<float>(kSize);
    c.outputSize[0]=c.outputSize[1]=static_cast<float>(kSize);
    c.lensOrigin[0]=c.lensOrigin[1]=static_cast<float>(kOrigin);
    c.lensSize[0]=c.lensSize[1]=256.0f;
    c.effect[0]=.69f;c.effect[1]=.18f;c.effect[2]=1.10f;c.effect[3]=dpi;
    c.sampleBoundsMax[0]=c.sampleBoundsMax[1]=static_cast<float>(kSize-1);
    c.readability[0]=c.readability[1]=1.0f;
    c.optical[1]=static_cast<float>(profile);
    c.optical[2]=profile==2?.92f:profile==1?1.0f:0.0f;
    c.optical[3]=radius*dpi;
    return c;
}

// No HWND, capture, DLL, or compositor is involved. The production HLSL runs
// on a real D3D11 device; staging readback observes its BGRA8 output.
class Renderer {
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> context_;
    Com<ID3D11VertexShader> vertex_;
    Com<ID3D11PixelShader> pixel_,frosted_;
    Com<ID3D11Buffer> constants_;
    Com<ID3D11SamplerState> sampler_;
    Com<ID3D11RasterizerState> rasterizer_;
    Com<ID3D11Texture2D> output_,staging_;
    Com<ID3D11RenderTargetView> target_;
public:
    Renderer(){
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,device_.Out(),nullptr,context_.Out()),"create WARP device");
        Com<ID3DBlob> vs,ps,fs;
        Compile(lume_materials::kLiquidGlassVertexShader,"vs_5_0",vs);
        Compile(lume_materials::kLiquidGlassPixelShader,"ps_5_0",ps);
        Compile(lume_materials::kFrostedGlassMaterial.pixelShader,"ps_5_0",fs);
        namespace shaders=lume_material_bytecode;
        const auto matches=[](Com<ID3DBlob>& code,const auto& bytes) {
            return code->GetBufferSize()==sizeof(bytes)&&
                std::memcmp(code->GetBufferPointer(),bytes,sizeof(bytes))==0;
        };
        Expect(matches(vs,shaders::vertex)&&matches(ps,shaders::liquid)&&matches(fs,shaders::frosted),
            "embedded material bytecode exactly matches original runtime compilation");
        Check(device_->CreateVertexShader(shaders::vertex,sizeof(shaders::vertex),nullptr,vertex_.Out()),"create vertex shader");
        Check(device_->CreatePixelShader(shaders::liquid,sizeof(shaders::liquid),nullptr,pixel_.Out()),"create pixel shader");
        Check(device_->CreatePixelShader(shaders::frosted,sizeof(shaders::frosted),nullptr,frosted_.Out()),"create frosted pixel shader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=sizeof(Constants);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(device_->CreateBuffer(&buffer,nullptr,constants_.Out()),"create constants");
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        Check(device_->CreateSamplerState(&sampler,sampler_.Out()),"create sampler");
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;rasterizer.DepthClipEnable=TRUE;
        Check(device_->CreateRasterizerState(&rasterizer,rasterizer_.Out()),"create rasterizer");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=texture.Height=kSize;texture.MipLevels=texture.ArraySize=1;
        texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.SampleDesc.Count=1;
        texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        Check(device_->CreateTexture2D(&texture,nullptr,output_.Out()),"create render target texture");
        Check(device_->CreateRenderTargetView(output_.value,nullptr,target_.Out()),"create render target view");
        texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device_->CreateTexture2D(&texture,nullptr,staging_.Out()),"create staging texture");
    }
    static void Compile(const char* source,const char* profile,Com<ID3DBlob>& code){
        Com<ID3DBlob> errors;
        const auto result=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",
            profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.Out(),errors.Out());
        if(FAILED(result)&&errors.value)std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
            static_cast<std::streamsize>(errors->GetBufferSize()));
        Check(result,"compile production HLSL");
    }
    Image Render(const Image& input,const Constants& constants,bool frosted=false){
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=texture.Height=kSize;texture.MipLevels=texture.ArraySize=1;
        texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.SampleDesc.Count=1;
        texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{input.data(),kSize*sizeof(Pixel),0};
        Com<ID3D11Texture2D> source;
        Com<ID3D11ShaderResourceView> sourceView;
        Check(device_->CreateTexture2D(&texture,&data,source.Out()),"create input texture");
        Check(device_->CreateShaderResourceView(source.value,nullptr,sourceView.Out()),"create input view");
        context_->UpdateSubresource(constants_.value,0,nullptr,&constants,0,0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_.value,nullptr,0);
        context_->PSSetShader(frosted?frosted_.value:pixel_.value,nullptr,0);
        context_->PSSetConstantBuffers(0,1,&constants_.value);
        context_->PSSetSamplers(0,1,&sampler_.value);
        context_->PSSetShaderResources(0,1,&sourceView.value);
        context_->RSSetState(rasterizer_.value);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(kSize),static_cast<float>(kSize),0,1};
        context_->RSSetViewports(1,&viewport);
        context_->OMSetRenderTargets(1,&target_.value,nullptr);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        constexpr float clear[4]{};
        context_->ClearRenderTargetView(target_.value,clear);
        context_->Draw(3,0);
        ID3D11ShaderResourceView* empty=nullptr;
        context_->PSSetShaderResources(0,1,&empty);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
        context_->CopyResource(staging_.value,output_.value);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context_->Map(staging_.value,0,D3D11_MAP_READ,0,&mapped),"read rendered pixels");
        Image result(kSize*kSize);
        for(UINT y=0;y<kSize;++y)std::memcpy(result.data()+y*kSize,
            static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,kSize*sizeof(Pixel));
        context_->Unmap(staging_.value,0);
        return result;
    }
};

void NeutralTransmission(Renderer& renderer){
    constexpr std::array<int,5> levels{0,32,128,192,240};
    // Recorded legacy profile-0 center values: this path must retain its tint.
    constexpr std::array<int,5> legacy{1,32,127,190,238};
    for(unsigned profile=0;profile<=2;++profile){
        std::cout<<"profile "<<profile<<" centers:";
        for(std::size_t i=0;i<levels.size();++i){
            const auto image=renderer.Render(Solid(static_cast<std::uint8_t>(levels[i])),Parameters(profile));
            const auto p=At(image,160,160);
            const int expected=profile==0?legacy[i]:levels[i];
            const int tolerance=profile==0?1:2;
            std::cout<<' '<<levels[i]<<"->"<<static_cast<unsigned>(p.red);
            const auto label="profile "+std::to_string(profile)+" gray "+std::to_string(levels[i])+
                " center RGB="+std::to_string(p.red)+","+std::to_string(p.green)+","+std::to_string(p.blue);
            Expect(std::abs(static_cast<int>(p.red)-expected)<=tolerance&&
                std::abs(static_cast<int>(p.green)-expected)<=tolerance&&
                std::abs(static_cast<int>(p.blue)-expected)<=tolerance,label+" preserves transmitted brightness");
            Expect(p.alpha==255,label+" remains opaque");
        }
        std::cout<<'\n';
    }
}
void CornerCoverage(Renderer& renderer){
    for(const float dpi:{1.0f,2.0f}){
        const auto rounded=renderer.Render(Solid(128),Parameters(1,dpi,8));
        const auto automatic=renderer.Render(Solid(128),Parameters(1,dpi));
        const int offset=static_cast<int>(3*dpi);
        const std::string label="8 DIP radius at "+std::to_string(static_cast<int>(96*dpi))+" DPI";
        Expect(At(rounded,kOrigin,kOrigin).alpha==0,label+" excludes exterior corner");
        Expect(At(rounded,kOrigin+offset,kOrigin+offset).alpha==255,label+" includes interior corner");
        Expect(At(automatic,kOrigin+offset,kOrigin+offset).alpha==0,label+" differs from default radius");
        Expect(At(rounded,160,160).alpha==255,label+" preserves center coverage");
    }
    const auto rounded=renderer.Render(Solid(128),Parameters(1,1,8));
    const auto alpha=At(rounded,kOrigin+2,kOrigin+2).alpha;
    Expect(alpha>0&&alpha<255,"8 DIP corner has fractional antialias coverage");
    std::cout<<"corner coverage: 8 DIP override at 96/192 DPI, fractional alpha="<<static_cast<unsigned>(alpha)<<'\n';
}
void PixelAreaCoverage(Renderer& renderer){
    // Independent area integration checks the entire curved edge, including
    // pixels whose centers are outside. One fractional pixel is insufficient
    // evidence: a hard SDF branch can discard half the antialias footprint.
    for(const bool frosted:{false,true})for(const float dpi:{1.f,1.25f,1.5f,2.f}) {
        const float radius=22.f*dpi;
        auto parameters=Parameters(1,dpi,22);
        if(frosted){parameters.effect[0]=-1.f;parameters.effect[2]=-radius;}
        const auto image=renderer.Render(Solid(128),parameters,frosted);
        unsigned sampled=0;
        int worst=0;
        double total=0;
        for(int y=0;y<static_cast<int>(std::ceil(radius));++y)
            for(int x=0;x<static_cast<int>(std::ceil(radius));++x) {
                const auto distance=std::hypot(radius-x-.5f,radius-y-.5f)-radius;
                if(std::abs(distance)>1.f)continue;
                unsigned inside=0;
                for(int sy=0;sy<64;++sy)for(int sx=0;sx<64;++sx)
                    if(std::hypot(radius-x-(sx+.5f)/64,radius-y-(sy+.5f)/64)<=radius)++inside;
                const int expected=static_cast<int>(std::lround(inside*255./4096));
                const int error=std::abs(static_cast<int>(At(image,kOrigin+x,kOrigin+y).alpha)-expected);
                worst=std::max(worst,error);total+=error;++sampled;
            }
        const double mean=sampled?total/sampled:255;
        Expect(sampled>20&&worst<=12&&mean<=4,
               std::string(frosted?"frosted":"liquid")+" glass must preserve per-pixel geometric coverage at "+std::to_string(static_cast<int>(dpi*96))+
               " DPI (worst="+std::to_string(worst)+", mean="+std::to_string(mean)+")");
        std::cout<<(frosted?"frosted":"liquid")<<" edge area "<<dpi*96<<" DPI: pixels="<<sampled<<" worst="<<worst<<" mean="<<mean<<'\n';
    }
}
void OpticalDetail(Renderer& renderer){
    Image checker(kSize*kSize);
    for(UINT y=0;y<kSize;++y)for(UINT x=0;x<kSize;++x){
        const auto gray=static_cast<std::uint8_t>(((x/16+y/16)%2)?192:32);
        checker[y*kSize+x]={gray,gray,gray,255};
    }
    const auto rendered=renderer.Render(checker,Parameters(1));
    const int dark=At(rendered,152,152).red,light=At(rendered,168,152).red;
    Expect(dark<70&&light>150&&light-dark>100,"checkerboard interior retains visible scene detail");
    int altered=0;
    for(int y=80;y<240;++y)for(int x=kOrigin+1;x<kOrigin+18;++x){
        const auto& p=At(rendered,x,y);
        if(p.alpha==255&&std::abs(static_cast<int>(p.red)-At(checker,x,y).red)>20)++altered;
    }
    Expect(altered>30,"rim retains sampled refraction/reflection instead of a flat copy");
    const auto gray=renderer.Render(Solid(128),Parameters(1));
    int glint=0;
    for(int y=kOrigin;y<kOrigin+32;++y)for(int x=kOrigin;x<kOrigin+160;++x){
        const auto& p=At(gray,x,y);
        if(p.alpha>=250)glint=std::max(glint,static_cast<int>(p.red));
    }
    Expect(glint>140,"edge highlight survives neutral body transmission");
    std::cout<<"optics: checker dark/light="<<dark<<'/'<<light<<", refracted rim pixels="<<altered<<", edge glint="<<glint<<'\n';
}
}
void AnnulusCoverage(Renderer& renderer){
    for(const bool frosted:{false,true}){
        auto c=Parameters(1,1.0f,128.0f);
        c.optical[0]=64.0f;
        if(frosted){c.effect[0]=-1;c.effect[2]=-128;}
        const auto image=renderer.Render(Solid(128),c,frosted);
        Expect(At(image,160,160).alpha==0,"radial glass center is genuinely transparent");
        Expect(At(image,160,80).alpha>200,"radial ring presents captured glass");
        Expect(At(image,40,40).alpha<8,"radial outer corners remain transparent");
    }
}
void AnnulusPixelAreaCoverage(Renderer& renderer){
    for(const bool frosted:{false,true})for(const float dpi:{1.f,1.25f,1.5f,2.f}){
        auto c=Parameters(1,dpi,160);
        c.lensSize[0]=c.lensSize[1]=320*dpi;c.optical[0]=76*dpi;
        if(frosted){c.effect[0]=-1;c.effect[2]=-160*dpi;}
        const auto image=renderer.Render(Solid(128),c,frosted);
        const float center=kOrigin+160*dpi,inner=76*dpi,outer=152*dpi;
        Expect(center-outer>=1&&center+outer+1<kSize,
               "annulus and antialias footprint fit entirely inside the render target");
        std::array<std::array<unsigned,4>,2> edgeQuadrants{};
        unsigned sampled=0;int worst=0;double total=0;
        for(int y=0;y<static_cast<int>(kSize);++y)for(int x=0;x<static_cast<int>(kSize);++x){
            const float distance=std::hypot(x+.5f-center,y+.5f-center);
            if(std::abs(distance-inner)>.708f&&std::abs(distance-outer)>.708f)continue;
            const unsigned edge=std::abs(distance-inner)<=.708f?0u:1u;
            const unsigned quadrant=(x+.5f>=center?1u:0u)+(y+.5f>=center?2u:0u);
            ++edgeQuadrants[edge][quadrant];
            unsigned covered=0;
            for(int sy=0;sy<32;++sy)for(int sx=0;sx<32;++sx){
                const float radial=std::hypot(x+(sx+.5f)/32-center,y+(sy+.5f)/32-center);
                if(radial>=inner&&radial<=outer)++covered;
            }
            const int expected=static_cast<int>(std::lround(covered*255./1024));
            const int error=std::abs(static_cast<int>(At(image,x,y).alpha)-expected);
            worst=std::max(worst,error);total+=error;++sampled;
        }
        const double mean=sampled?total/sampled:255;
        for(unsigned edge=0;edge<2;++edge)for(unsigned quadrant=0;quadrant<4;++quadrant)
            Expect(edgeQuadrants[edge][quadrant]>100,
                   "annulus samples both edges in every quadrant: edge="+std::to_string(edge)+
                   " quadrant="+std::to_string(quadrant));
        // Independent CPU integration gives a 13/255 peak error for the 8x8
        // grid at 192 DPI (pixel 346,48); retain the stricter mean-area bound.
        Expect(sampled>100&&worst<=13&&mean<=4,
               std::string(frosted?"frosted":"liquid")+" annulus retains fractional inner/outer pixel area at "+std::to_string(static_cast<int>(dpi*96))+
               " DPI (worst="+std::to_string(worst)+", mean="+std::to_string(mean)+")");
        std::cout<<(frosted?"frosted":"liquid")<<" annulus edge area "<<dpi*96<<" DPI: pixels="<<sampled<<" worst="<<worst<<" mean="<<mean<<'\n';
    }
}
int main(){
    try{
        Renderer renderer;
        AnnulusCoverage(renderer);
        AnnulusPixelAreaCoverage(renderer);
        NeutralTransmission(renderer);
        CornerCoverage(renderer);
        PixelAreaCoverage(renderer);
        OpticalDetail(renderer);
    }catch(const std::exception& error){
        std::cerr<<"ERROR: "<<error.what()<<'\n';return 2;
    }
    if(failures){std::cerr<<"FAIL: "<<failures<<" shader regression assertions\n";return 1;}
    std::cout<<"PASS: production liquid-glass D3D11 shader transmission, corner coverage, and optical detail\n";
}
