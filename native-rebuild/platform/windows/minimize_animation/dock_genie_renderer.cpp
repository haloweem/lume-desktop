#include "dock_genie_renderer.h"
#include "dock_genie_packet.h"
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <vector>
#include <cmath>

namespace lume::platform::windows::dock_genie {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using namespace minimize_detail;
namespace {
// The cross section facing the Dock begins to narrow first. Both the
// leading and trailing edges finish at the icon with zero end velocity.
constexpr char Shader[] = R"(
cbuffer Scene : register(b0) {
    float4 windowRect, iconRect;
    float2 canvasSize;
    float progress, edge;
    float zoom;
};
struct Vertex { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
float ease(float t) { t=saturate(t); return t*t*(3-2*t); }
Vertex vs(uint id : SV_VertexID) {
    float row=(id/2)/96.0;
    float column=id%2;
    bool side=edge>=2;
    float2 uv=side ? float2(row,column) : float2(column,row);
    float2 source=windowRect.xy+uv*windowRect.zw;
    float2 target=iconRect.xy+uv*iconRect.zw;
    float2 xy=lerp(source,target,ease(progress));
    if (zoom<.5) {
        // Measure depth from the far edge of the window toward the icon.
        // Each border follows a smooth shoulder, a bend, and a narrow neck.
        bool reverse=edge==1 || edge==2;
        float depth=reverse ? 1-row : row;
        float gather=ease(progress/.52);
        float feed=ease((progress-.20)/.80);
        float along=lerp(depth,1,feed);
        float neck=smoothstep(.13,.93,along);
        neck=neck*neck*(3-2*neck);
        float crossMix=gather*neck;
        if (side) xy.y=lerp(source.y,target.y,crossMix);
        else xy.x=lerp(source.x,target.x,crossMix);

        float start=side ? windowRect.x : windowRect.y;
        float finish=side ? iconRect.x : iconRect.y;
        float extent=side ? windowRect.z : windowRect.w;
        float iconExtent=side ? iconRect.z : iconRect.w;
        float farEdge=reverse ? start+extent : start;
        float lip=reverse ? start : start+extent;
        float targetFar=reverse ? finish+iconExtent : finish;
        float targetLip=reverse ? finish : finish+iconExtent;
        // A deeply overlapping window can extend beyond the icon. Keep its
        // sections in order even when the mouth travels back into the window.
        float travel=(targetLip-lip)*(reverse ? -1 : 1);
        float reach=lerp(feed,ease(progress/.64),saturate(1+travel/max(extent,1)));
        float axial=lerp(lerp(farEdge,targetFar,feed),lerp(lip,targetLip,reach),depth);
        if (side) xy.x=axial; else xy.y=axial;
    }
    Vertex v;
    v.pos=float4(xy/canvasSize*float2(2,-2)+float2(-1,1),0,1);
    v.uv=uv;
    return v;
}
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);
float4 ps(Vertex v) : SV_TARGET {
    float2 size=lerp(windowRect.zw,iconRect.zw,ease(progress));
    float radius=min(8.0,min(size.x,size.y)*.15);
    float2 q=abs((v.uv-.5)*size)-size*.5+radius;
    float mask=saturate(.5-(length(max(q,0))+min(max(q.x,q.y),0)-radius));
    if (zoom<.5) mask*=1-ease((progress-.86)/.14);
    return float4(picture.Sample(linearClamp,v.uv).rgb*mask,mask);
}
)";
struct Constants { float window[4], icon[4], canvas[2], progress, edge, zoom, pad[3]; };
bool ValidRect(RECT r) {
    return r.left >= -100000 && r.top >= -100000 && r.right <= 100000 && r.bottom <= 100000 &&
           r.right > r.left && r.bottom > r.top;
}
struct Playback {
    HWND ghost=nullptr, source=nullptr;
    Motion motion{};
    int duration=450;
    Clock::time_point start, lastFrame{};
    Constants constants{};
    ComPtr<IDXGISwapChain1> chain;
    ComPtr<ID3D11RenderTargetView> output;
    ComPtr<ID3D11ShaderResourceView> image;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ~Playback() { if (ghost && IsWindow(ghost)) DestroyWindow(ghost); }
};
LRESULT CALLBACK OverlayProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) return HTTRANSPARENT;
    if (message==WM_ERASEBKGND) return 1;
    if (message==WM_PAINT) { ValidateRect(hwnd,nullptr); return 0; }
    return DefWindowProcW(hwnd,message,w,l);
}
}

struct Renderer::Impl {
    SharedControl* state=nullptr;
    HWND receiver=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Buffer> parameters;
    std::vector<std::unique_ptr<Playback>> playing;
    ~Impl() {
        if (state) InterlockedExchange(&state->rendererWindow,0);
        playing.clear();
        if (receiver) DestroyWindow(receiver);
    }
    bool Device() {
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)) &&
            FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&device,&level,&context))) return false;
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))) ||
            FAILED(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition)))) return false;
        ComPtr<ID3DBlob> v,p;
        if (FAILED(D3DCompile(Shader,sizeof(Shader),nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&v,nullptr)) ||
            FAILED(D3DCompile(Shader,sizeof(Shader),nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&p,nullptr)) ||
            FAILED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
            FAILED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))) return false;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=sizeof(Constants); buffer.Usage=D3D11_USAGE_DEFAULT;
        buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC sample{};
        sample.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD=D3D11_FLOAT32_MAX; sample.ComparisonFunc=D3D11_COMPARISON_NEVER;
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.CullMode=D3D11_CULL_NONE; rasterDesc.FillMode=D3D11_FILL_SOLID;
        rasterDesc.DepthClipEnable=TRUE;
        return SUCCEEDED(device->CreateBuffer(&buffer,nullptr,&parameters)) &&
               SUCCEEDED(device->CreateSamplerState(&sample,&sampler)) &&
               SUCCEEDED(device->CreateRasterizerState(&rasterDesc,&raster));
    }
    bool Draw(Playback& a,float fraction) {
        const bool outgoing=a.motion==Motion::Close || a.motion==Motion::Minimize;
        a.constants.progress=outgoing ? fraction : 1.f-fraction;
        context->UpdateSubresource(parameters.Get(),0,nullptr,&a.constants,0,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vs.Get(),nullptr,0);
        context->PSSetShader(ps.Get(),nullptr,0);
        context->VSSetConstantBuffers(0,1,parameters.GetAddressOf());
        context->PSSetConstantBuffers(0,1,parameters.GetAddressOf());
        context->PSSetShaderResources(0,1,a.image.GetAddressOf());
        context->PSSetSamplers(0,1,sampler.GetAddressOf());
        context->OMSetRenderTargets(1,a.output.GetAddressOf(),nullptr);
        context->RSSetState(raster.Get());
        D3D11_VIEWPORT viewport{0,0,a.constants.canvas[0],a.constants.canvas[1],0,1};
        context->RSSetViewports(1,&viewport);
        constexpr float clear[4]{};
        context->ClearRenderTargetView(a.output.Get(),clear);
        context->Draw(194,0);
        if (FAILED(a.chain->Present(0,0))) return false;
        InterlockedIncrement(&state->dockFrames);
        return true;
    }
    HWND Submit(HWND source,const COPYDATASTRUCT* data) {
        if (!Read(state->enabled) || !Read(state->dockOnly) || !IsWindow(source) || !data ||
            data->dwData!=SnapshotMessage || !data->lpData || data->cbData<sizeof(Packet)) return nullptr;
        DWORD pid=0;
        GetWindowThreadProcessId(source,&pid);
        if (state->targetProcess && static_cast<DWORD>(state->targetProcess)!=pid) return nullptr;
        const auto activeSource=std::any_of(std::begin(state->processes),std::end(state->processes),
            [pid](auto& lease) {
                return Read(lease.pid)==static_cast<LONG>(pid) && (Read(lease.entries)>0 || Read(lease.workers)>0);
            });
        if (!activeSource) return nullptr;
        Packet packet{};
        std::memcpy(&packet,data->lpData,sizeof(packet));
        if (!ValidRect(packet.window) || !ValidRect(packet.destination) || packet.edge<0 || packet.edge>3 ||
            packet.motion>Motion::Zoom) return nullptr;
        const auto w=packet.window.right-packet.window.left,h=packet.window.bottom-packet.window.top;
        const auto bytes=minimize_animation::SnapshotBytes(w,h);
        if (!bytes || data->cbData!=sizeof(Packet)+bytes) return nullptr;
        RECT canvas{};
        UnionRect(&canvas,&packet.window,&packet.destination);
        const int width=canvas.right-canvas.left,height=canvas.bottom-canvas.top;
        if (!minimize_animation::SnapshotBytes(width,height,minimize_animation::MaxCanvasBytes)) return nullptr;
        playing.erase(std::remove_if(playing.begin(),playing.end(),
            [source](const auto& current){return current->source==source;}),playing.end());
        if (playing.size()>=4) return nullptr;
        auto a=std::make_unique<Playback>();
        a->source=source; a->motion=packet.motion; a->duration=Read(state->durationMs);
        a->ghost=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|
            WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT,OverlayClass,L"LumeDockGenie",WS_POPUP,canvas.left,canvas.top,
            width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!a->ghost || !SetLayeredWindowAttributes(a->ghost,0,255,LWA_ALPHA)) return nullptr;
        auto relative=[&](RECT rect,float (&values)[4]) {
            values[0]=float(rect.left-canvas.left); values[1]=float(rect.top-canvas.top);
            values[2]=float(rect.right-rect.left); values[3]=float(rect.bottom-rect.top);
        };
        relative(packet.window,a->constants.window); relative(packet.destination,a->constants.icon);
        a->constants.edge=float(packet.edge); a->constants.zoom=packet.motion==Motion::Zoom;
        a->constants.canvas[0]=float(width); a->constants.canvas[1]=float(height);
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=w; texture.Height=h; texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;
        texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM; texture.Usage=D3D11_USAGE_IMMUTABLE;
        texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{static_cast<const BYTE*>(data->lpData)+sizeof(Packet),UINT(w*4),0};
        ComPtr<ID3D11Texture2D> picture,back;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width=width; desc.Height=height; desc.Format=texture.Format; desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Scaling=DXGI_SCALING_STRETCH;
        if (FAILED(device->CreateTexture2D(&texture,&initial,&picture)) ||
            FAILED(device->CreateShaderResourceView(picture.Get(),nullptr,&a->image)) ||
            FAILED(factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&a->chain)) ||
            FAILED(a->chain->GetBuffer(0,IID_PPV_ARGS(&back))) ||
            FAILED(device->CreateRenderTargetView(back.Get(),nullptr,&a->output)) ||
            FAILED(composition->CreateTargetForHwnd(a->ghost,TRUE,&a->target)) ||
            FAILED(composition->CreateVisual(&a->visual)) ||
            FAILED(a->visual->SetContent(a->chain.Get())) || FAILED(a->target->SetRoot(a->visual.Get())) ||
            !Draw(*a,0) || FAILED(composition->Commit()) || FAILED(composition->WaitForCommitCompletion())) return nullptr;
        ShowWindow(a->ghost,SW_SHOWNOACTIVATE);
        a->start=Clock::now();
        const auto hwnd=a->ghost;
        playing.push_back(std::move(a));
        return hwnd;
    }
    static LRESULT CALLBACK Receive(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        if (message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        if (message==CancelPlayback) {
            auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
            if (self) {
                auto& list=self->playing;
                list.erase(std::remove_if(list.begin(),list.end(),[&](const auto& a) {
                    return a->source==reinterpret_cast<HWND>(w) && a->ghost==reinterpret_cast<HWND>(l);
                }),list.end());
                if (list.empty()) self->context->ClearState();
            }
            return 0;
        }
        if (message==WM_COPYDATA) {
            auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
            try {return self ? HandleToLong(self->Submit(reinterpret_cast<HWND>(w),
                reinterpret_cast<const COPYDATASTRUCT*>(l))) : 0;} catch (...) {return 0;}
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
Renderer::Renderer():impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer()=default;
bool Renderer::Start(SharedControl& state) {
    impl_->state=&state;
    WNDCLASSW wc{}; wc.lpfnWndProc=OverlayProc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=OverlayClass;
    if (!RegisterClassW(&wc)) return false;
    wc.lpfnWndProc=Impl::Receive;
    wc.lpszClassName=L"LumeDockGenieReceiver";
    if (!impl_->Device() || !RegisterClassW(&wc)) return false;
    impl_->receiver=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,impl_.get());
    InterlockedExchange(&state.rendererWindow,HandleToLong(impl_->receiver));
    return impl_->receiver!=nullptr;
}
bool Renderer::Playing() const {return !impl_->playing.empty();}
void Renderer::Frame() {
    const auto now=Clock::now();
    auto& plays=impl_->playing;
    plays.erase(std::remove_if(plays.begin(),plays.end(),[&](auto& a) {
        if (!IsWindow(a->ghost)) return true;
        if (a->lastFrame==Clock::time_point{}) {
            const float t=std::chrono::duration<float,std::milli>(now-a->start).count()/a->duration;
            if (!impl_->Draw(*a,std::clamp(t,0.f,1.f))) return true;
            if (t<1) return false;
            if (a->motion!=Motion::Minimize && a->motion!=Motion::Close &&
                SetPropW(a->ghost,LastFrameProperty,reinterpret_cast<HANDLE>(1))) {
                a->lastFrame=now;
                return false;
            }
        } else {
            // Keep the last full-size texture until the real surface is back
            // in composition, including when the UI thread resumes late.
            DWORD cloaked=0;
            if (now-a->lastFrame<std::chrono::milliseconds(100) &&
                SUCCEEDED(DwmGetWindowAttribute(a->source,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked)
                return false;
        }
        InterlockedIncrement(&impl_->state->dockCompleted);
        return true;
    }),plays.end());
    if (plays.empty()) impl_->context->ClearState();
}
}
