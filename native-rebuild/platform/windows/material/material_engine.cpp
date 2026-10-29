#include "material_api.h"
#include "../lume_window_ids.h"
#include "annular_region.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "material-shader-bytecode.h"
#include "desktop-window.h"
#include "glass-wake.h"
#include "glass-interaction.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

using Microsoft::WRL::ComPtr;

namespace {

// Capture and owner notifications normally wake a glass worker immediately.
// These bounded waits only cover missed WinEvent hooks or a hidden owner; keep
// them long enough to avoid a timer-driven wakeup for every widget.
constexpr DWORD kIdleWakeFallbackMs = 64;
constexpr DWORD kHiddenResumeWaitMs = 250;

// Keep the native HWND fixed while moving a widget-sized composition visual.
// A full-desktop swap chain per widget wastes two desktop-sized GPU textures.
struct GlassOverlay { float x, y, width, height, radius; };

struct GlassInstance {
  std::shared_ptr<lume_glass::WakeSignal> wake = std::make_shared<lume_glass::WakeSignal>();
  HWND owner = nullptr;
  std::atomic<HWND> sceneAnchor{nullptr};
  std::atomic<HWND> surface{nullptr};
  std::atomic<bool> stop{false};
  std::atomic<int> lensX{0};
  std::atomic<int> lensY{0};
  std::atomic<int> lensWidth{1};
  std::atomic<int> lensHeight{1};
  std::atomic<int> bottomInset{0};
  std::atomic<bool> frosted{false};
  std::atomic<bool> panelSurface{false};
  std::atomic<float> cornerRadius{0};
  std::atomic<float> innerRadius{0};
  std::atomic<float> opacity{1};
  // Optical profiles are intentionally separate from the material toggle.
  // Profile 0 is the existing music/control-bar path; profile 1 is the
  // calendar component with dynamic thickness and fluid refraction.
  std::atomic<uint32_t> opticalProfile{0};
  std::thread worker;
  winrt::handle workerExited{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
  std::atomic<unsigned> frames{0};
  std::atomic<unsigned> sceneChecks{0};
  std::atomic<unsigned> sources{0};
  std::atomic<unsigned> readySources{0};
  std::atomic<unsigned> desktopIconSources{0};
  std::atomic<unsigned> desktopIconReady{0};
  std::atomic<unsigned> captureFrames{0};
  std::atomic<unsigned> deferredScenes{0};
  std::atomic<unsigned> surfaceHides{0};
  std::atomic<unsigned> scenePixels{0};
  std::atomic<unsigned> surfacePixels{0};
  std::atomic<unsigned> revision{0};
  std::atomic<HRESULT> lastError{S_OK};
  std::atomic<bool> sampleRequested{false};
  std::atomic<unsigned> samples{0};
  std::atomic<uint32_t> sampleColor{0};
  // Internal diagnostics can sample the committed surface independently of
  // foreground controls and any windows covering it on screen.
  std::atomic<bool> outputSampleRequested{false};
  std::atomic<unsigned> outputSamples{0};
  std::atomic<uint32_t> outputSampleColor{0};
  std::atomic<bool> initialized{false};
  std::atomic<bool> frameReady{false};
  std::atomic<bool> failed{false};
  std::atomic<int> publishedState{-1};
  std::mutex discsMutex;
  std::mutex interactionMutex;
  lume_glass::InteractionInput interactionInput;
  lume_glass::GlassInteraction interaction; // Render worker owns presentation state.
  std::atomic<float> interactionPressure{0};
  std::atomic<float> interactionMotion{0};
  std::atomic<bool> readabilityEnabled{true};
  std::atomic<bool> lightForeground{true};
  std::vector<GlassOverlay> discs;
  std::vector<GlassOverlay> panels;
  // V7 explicit lens and content layer (see material_api.h). Written on the UI
  // thread under contentMutex; the worker reads them each frame.
  std::mutex contentMutex;
  bool lensOverride = false, contentEnabled = false;
  float contentOpacity = 1;  // net content opacity from a timeline, 1 otherwise
  std::atomic<int> stageWidth{0}, stageHeight{0};  // owner client px while the lens is explicit
  std::atomic<int> stageX{0}, stageY{0};  // owner client origin (screen px) while the lens is explicit
  // lensOverride 2: origin and stage fixed when the timeline is handed over
  // (under contentMutex).
  bool anchored = false;
  POINT anchor{};
  RECT anchorStage{};
  // An explicit lens in exact (sub-pixel) screen coordinates, so its edge
  // moves as smoothly as the content scaled inside it.
  std::atomic<bool> lensPrecise{false};
  std::atomic<float> lensXf{0}, lensYf{0}, lensWidthf{1}, lensHeightf{1};
  float lensRect[4]{}, contentRect[4]{};
  std::vector<uint32_t> contentPixels;
  uint32_t contentPixelWidth = 0, contentPixelHeight = 0, contentGeneration = 0;
  unsigned contentRevision = 0;
  std::vector<LumeMaterialKeyframe> timeline;
  float timelineStep = 0;
  int64_t timelineStart = 0;
};

struct OutputCapture {
  ComPtr<ID3D11ShaderResourceView> desktopView;
  RECT bounds = {};
};

struct GlassConstants {
  float surfaceOrigin[2];
  float surfaceSize[2];
  float outputOrigin[2];
  float outputSize[2];
  float lensOrigin[2];
  float lensSize[2];
  float effect[4];
  float sampleBoundsMin[2];
  float sampleBoundsMax[2];
  float interaction[4];
  float motion[4];
  float readability[4];
  float optical[4]; // annulus inner radius, profile, thickness scale, outer radius override
};

std::mutex g_mutex;
std::unordered_map<uint32_t, std::unique_ptr<GlassInstance>> g_glasses;
uint32_t g_nextId = 1;

void LogNative(const char* stage, HRESULT result = S_OK) {
  char buffer[256] = {};
  std::snprintf(buffer, sizeof(buffer), "%s hr=0x%08lx\n", stage, static_cast<unsigned long>(result));
  OutputDebugStringA(buffer);
  char logPath[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableA("LUME_GLASS_LOG", logPath, ARRAYSIZE(logPath));
  if (length > 0 && length < ARRAYSIZE(logPath)) {
    // Frame-timing lines go to one file per worker: concurrent appends from
    // several glass workers to one file interleave and lose lines.
    if (stage[0] == 'T' && stage[1] == ' ')
      std::snprintf(logPath + length, ARRAYSIZE(logPath) - length, ".%lu", GetCurrentThreadId());
    std::ofstream file(logPath, std::ios::app);
    if (file) file << buffer;
  }
}


LRESULT CALLBACK SurfaceWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
  }
  if (message == WM_APP + 1) {
    auto* glass = reinterpret_cast<GlassInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (glass && glass->frameReady && IsWindowVisible(glass->owner))
      SetWindowPos(hwnd, glass->owner, 0, 0, 0, 0,
                   SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    else ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  if (message == WM_NCHITTEST) return HTTRANSPARENT;
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message == WM_ERASEBKGND) return 1;
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

HMODULE MaterialModule() {
  HMODULE module=nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&SurfaceWindowProc),&module);
  return module;
}
bool RegisterSurfaceClass() {
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc=SurfaceWindowProc;
  windowClass.hInstance=MaterialModule();
  windowClass.lpszClassName=lume::window_ids::GlassSurfaceClass;
  return RegisterClassW(&windowClass)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
}
void NotifyStatus(GlassInstance* glass) {
  const int state=glass->failed?3:glass->frameReady?2:glass->initialized?3:1;
  if(glass->publishedState.exchange(state)!=state)
    PostMessageW(glass->owner,LumeMaterialStatusChangedMessage,0,0);
}
void SignalInitialization(GlassInstance* glass, bool succeeded) {
  glass->initialized = succeeded;
  glass->failed = !succeeded;
  if (!succeeded && SUCCEEDED(glass->lastError.load())) glass->lastError = E_FAIL;
  NotifyStatus(glass);
}

#include "glass-capture.h"
#include "glass-scene.h"

RECT OwnerMonitorBounds(HWND owner, const RECT& fallback) {
  MONITORINFO info = { sizeof(MONITORINFO) };
  const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
  if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcMonitor;
  return fallback;
}

void RefreshLiveLens(GlassInstance* glass) {
  if (!glass->owner || !IsWindow(glass->owner)) return;
  RECT client = {};
  POINT origin = {0, 0};
  if (!GetClientRect(glass->owner, &client) || !ClientToScreen(glass->owner, &origin)) return;
  glass->lensPrecise.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(glass->contentMutex);
    if (!glass->lensOverride) glass->stageWidth.store(0, std::memory_order_relaxed);
    if (glass->lensOverride) {
      const float scale = GetDpiForWindow(glass->owner) / 96.0f;
      if (glass->anchored) {
        // An anchored lens is placed from the owner's origin when its timeline
        // was handed over, so a window resized and moved per frame on its own
        // thread cannot pull the glass a frame behind; its stage is the
        // timeline's whole extent.
        origin = glass->anchor;
        glass->stageWidth.store(glass->anchorStage.right - glass->anchorStage.left, std::memory_order_relaxed);
        glass->stageHeight.store(glass->anchorStage.bottom - glass->anchorStage.top, std::memory_order_relaxed);
        glass->stageX.store(glass->anchorStage.left, std::memory_order_relaxed);
        glass->stageY.store(glass->anchorStage.top, std::memory_order_relaxed);
      } else {
        glass->stageWidth.store(client.right - client.left, std::memory_order_relaxed);
        glass->stageHeight.store(client.bottom - client.top, std::memory_order_relaxed);
        glass->stageX.store(origin.x, std::memory_order_relaxed);
        glass->stageY.store(origin.y, std::memory_order_relaxed);
      }
      const auto px = [scale](float dip) { return static_cast<int>(std::lround(dip * scale)); };
      glass->lensX.store(origin.x + px(glass->lensRect[0]), std::memory_order_relaxed);
      glass->lensY.store(origin.y + px(glass->lensRect[1]), std::memory_order_relaxed);
      glass->lensWidth.store((std::max)(1, px(glass->lensRect[2])), std::memory_order_relaxed);
      glass->lensHeight.store((std::max)(1, px(glass->lensRect[3])), std::memory_order_relaxed);
      glass->lensXf.store(origin.x + glass->lensRect[0] * scale, std::memory_order_relaxed);
      glass->lensYf.store(origin.y + glass->lensRect[1] * scale, std::memory_order_relaxed);
      glass->lensWidthf.store((std::max)(1.0f, glass->lensRect[2] * scale), std::memory_order_relaxed);
      glass->lensHeightf.store((std::max)(1.0f, glass->lensRect[3] * scale), std::memory_order_relaxed);
      glass->lensPrecise.store(true, std::memory_order_relaxed);
      return;
    }
  }
  glass->lensX.store(origin.x, std::memory_order_relaxed);
  glass->lensY.store(origin.y, std::memory_order_relaxed);
  glass->lensWidth.store((std::max)(1, static_cast<int>(client.right - client.left)), std::memory_order_relaxed);
  const int inset = MulDiv(glass->bottomInset.load(std::memory_order_relaxed), GetDpiForWindow(glass->owner), 96);
  glass->lensHeight.store((std::max)(1, static_cast<int>(client.bottom - client.top) - inset), std::memory_order_relaxed);
}

bool DrawLens(GlassInstance* glass, ID3D11DeviceContext* context, const OutputCapture& output,
              ID3D11Buffer* constantBuffer, int surfaceLeft, int surfaceTop,
              int surfaceWidth, int surfaceHeight, const RECT& sampleBounds,
              const GlassOverlay* disc = nullptr) {
  if (!output.desktopView) return true;
  const float dpi = static_cast<float>(GetDpiForWindow(glass->owner)) / 96.0f;
  const bool precise = glass->lensPrecise.load(std::memory_order_relaxed);
  const float baseX = precise ? glass->lensXf.load(std::memory_order_relaxed) : static_cast<float>(glass->lensX.load(std::memory_order_relaxed));
  const float baseY = precise ? glass->lensYf.load(std::memory_order_relaxed) : static_cast<float>(glass->lensY.load(std::memory_order_relaxed));
  const float lensX = baseX + (disc ? disc->x * dpi : 0);
  const float lensY = baseY + (disc ? disc->y * dpi : 0);
  const float lensWidth = disc ? disc->width * dpi : precise ? glass->lensWidthf.load(std::memory_order_relaxed)
      : static_cast<float>((std::max)(1, glass->lensWidth.load(std::memory_order_relaxed)));
  const float lensHeight = disc ? disc->height * dpi : precise ? glass->lensHeightf.load(std::memory_order_relaxed)
      : static_cast<float>((std::max)(1, glass->lensHeight.load(std::memory_order_relaxed)));
  const int padding = 34;
  RECT scissor = {
    (std::max)(static_cast<int>(output.bounds.left), static_cast<int>(std::floor(lensX)) - padding) - surfaceLeft,
    (std::max)(static_cast<int>(output.bounds.top), static_cast<int>(std::floor(lensY)) - padding) - surfaceTop,
    (std::min)(static_cast<int>(output.bounds.right), static_cast<int>(std::ceil(lensX + lensWidth)) + padding) - surfaceLeft,
    (std::min)(static_cast<int>(output.bounds.bottom), static_cast<int>(std::ceil(lensY + lensHeight)) + padding) - surfaceTop,
  };
  if (scissor.left >= scissor.right || scissor.top >= scissor.bottom) return true;
  GlassConstants values = {};
  values.surfaceOrigin[0] = static_cast<float>(surfaceLeft);
  values.surfaceOrigin[1] = static_cast<float>(surfaceTop);
  values.surfaceSize[0] = static_cast<float>(surfaceWidth);
  values.surfaceSize[1] = static_cast<float>(surfaceHeight);
  values.outputOrigin[0] = static_cast<float>(output.bounds.left);
  values.outputOrigin[1] = static_cast<float>(output.bounds.top);
  values.outputSize[0] = static_cast<float>(output.bounds.right - output.bounds.left);
  values.outputSize[1] = static_cast<float>(output.bounds.bottom - output.bounds.top);
  values.lensOrigin[0] = static_cast<float>(lensX);
  values.lensOrigin[1] = static_cast<float>(lensY);
  values.lensSize[0] = static_cast<float>(lensWidth);
  values.lensSize[1] = static_cast<float>(lensHeight);
  values.effect[0] = 0.69f;
  values.effect[1] = 0.18f;
  values.effect[2] = disc && disc->radius >= 0 ? -(std::max)(0.001f, disc->radius * dpi) : 1.10f;
  if (!disc && glass->frosted) {
    // Detached editor/picker HWNDs retain the original overlay shader profile.
    // The whole-widget profile (-1) has different tint and blur parameters.
    values.effect[0] = glass->panelSurface ? 0.69f : -1.0f;
    const float radius=glass->cornerRadius.load(std::memory_order_relaxed);
    values.effect[2] = -(radius>0?radius:22.0f) * dpi;
  }
  values.effect[3] = dpi;
  values.sampleBoundsMin[0] = static_cast<float>(sampleBounds.left);
  values.sampleBoundsMin[1] = static_cast<float>(sampleBounds.top);
  values.sampleBoundsMax[0] = static_cast<float>(sampleBounds.right - 1);
  values.sampleBoundsMax[1] = static_cast<float>(sampleBounds.bottom - 1);
  if (!disc && !glass->frosted) {
    const auto& state = glass->interaction;
    values.interaction[0] = state.pressure.value;
    values.interaction[1] = state.x.value;
    values.interaction[2] = state.y.value;
    values.interaction[3] = state.light;
    values.motion[0] = state.motionX.value;
    values.motion[1] = state.motionY.value;
    values.motion[2] = state.lightX.value;
    values.motion[3] = state.lightY.value;
  values.readability[0] = glass->readabilityEnabled ? 1.0f : 0.0f;
    values.readability[1] = glass->lightForeground ? 1.0f : 0.0f;
  }
  values.optical[0] = disc ? 0 : glass->innerRadius.load(std::memory_order_relaxed) * dpi;
  values.optical[1] = static_cast<float>(glass->opticalProfile.load(std::memory_order_relaxed));
  values.optical[2] = values.optical[1] > 2.5f ? 0.55f : values.optical[1] > 1.5f ? 0.92f : values.optical[1] > 0.5f ? 1.0f : 0.0f;
  values.optical[3] = glass->cornerRadius.load(std::memory_order_relaxed) * dpi;
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
  std::memcpy(mapped.pData, &values, sizeof(values));
  context->Unmap(constantBuffer, 0);
  context->RSSetScissorRects(1, &scissor);
  context->PSSetShaderResources(0, 1, output.desktopView.GetAddressOf());
  context->Draw(3, 0);
  ID3D11ShaderResourceView* empty = nullptr;
  context->PSSetShaderResources(0, 1, &empty);
  return true;
}

uint32_t SampleTexturePixel(ID3D11Device* device, ID3D11DeviceContext* context,
                            ID3D11Texture2D* source, int x, int y) {
  if (!device || !context || !source || x < 0 || y < 0) return 0;
  D3D11_TEXTURE2D_DESC sourceDesc{};
  source->GetDesc(&sourceDesc);
  if (static_cast<UINT>(x) >= sourceDesc.Width || static_cast<UINT>(y) >= sourceDesc.Height ||
      sourceDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return 0;
  D3D11_TEXTURE2D_DESC stagingDesc = {};
  stagingDesc.Width = stagingDesc.Height = stagingDesc.ArraySize = stagingDesc.MipLevels = 1;
  stagingDesc.Format = sourceDesc.Format;
  stagingDesc.SampleDesc.Count = 1;
  stagingDesc.Usage = D3D11_USAGE_STAGING;
  stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) return 0;
  const D3D11_BOX region = {static_cast<UINT>(x), static_cast<UINT>(y), 0,
      static_cast<UINT>(x + 1), static_cast<UINT>(y + 1), 1};
  context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, &region);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 0;
  const auto* pixel = static_cast<const uint8_t*>(mapped.pData);
  const uint32_t color = static_cast<uint32_t>(pixel[2]) << 16 |
      static_cast<uint32_t>(pixel[1]) << 8 | pixel[0] |
      static_cast<uint32_t>(pixel[3]) << 24;
  context->Unmap(staging.Get(), 0);
  return color;
}

bool InitializeAndRun(GlassInstance* glass) {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  LogNative("InitializeAndRun");
  const int surfaceLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int surfaceTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int surfaceWidth = (std::max)(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
  const int surfaceHeight = (std::max)(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
  const DWORD topmost=(GetWindowLongPtrW(glass->owner,GWL_EXSTYLE)&WS_EX_TOPMOST)?WS_EX_TOPMOST:0;
  glass->surface = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | topmost,
      lume::window_ids::GlassSurfaceClass, L"Lume Liquid Glass", WS_POPUP,
      surfaceLeft, surfaceTop, surfaceWidth, surfaceHeight, nullptr, nullptr, MaterialModule(), glass);
  if (!glass->surface) { SignalInitialization(glass, false); return false; }
  // Screenshots must contain the visible material. Feedback is prevented by
  // GlassScene's window-only SourcesBelow traversal, excluding this surface;
  // lower native surfaces arrive through SurfaceFeed, never monitor capture.
  SetWindowDisplayAffinity(glass->surface, WDA_NONE);
  SetPropW(glass->surface, lume::window_ids::GlassSurfaceOwnerProp, reinterpret_cast<HANDLE>(glass->owner));
  g_glassCaptureRegistry.RegisterSurface(glass->surface, glass->owner);
  struct SurfaceRegistration {
    HWND surface;
    ~SurfaceRegistration() { g_glassCaptureRegistry.UnregisterSurface(surface); }
  } surfaceRegistration{glass->surface};
  lume_glass::WindowEvents windowEvents(glass->wake);
  // Capture lower widget surfaces in Z-order, never the current lens itself.
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIAdapter> adapter;
  HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(factoryResult)) { LogNative("CreateDXGIFactory1", factoryResult); SignalInitialization(glass, false); return false; }
  HRESULT adapterResult = factory->EnumAdapters(0, adapter.GetAddressOf());
  if (FAILED(adapterResult)) { LogNative("EnumAdapters", adapterResult); SignalInitialization(glass, false); return false; }
  LogNative("FactoryAndAdapter");
  std::shared_ptr<GlassCaptureRegistry::Graphics> graphics;
  try { graphics = g_glassCaptureRegistry.AcquireGraphics(adapter.Get()); }
  catch (const winrt::hresult_error& error) {
    LogNative("D3D11CreateDevice", error.code()); SignalInitialization(glass, false); return false;
  }
  LogNative("D3D11CreateDevice");
  ComPtr<ID3D11Device> device = graphics->device;
  ComPtr<ID3D11DeviceContext> context = graphics->context;
  GlassScene scene;
  if (!scene.Initialize(graphics, glass->wake, surfaceLeft, surfaceTop, surfaceWidth, surfaceHeight)) {
    SignalInitialization(glass, false); return false;
  }
  OutputCapture output;
  output.desktopView = scene.View();
  output.bounds = {surfaceLeft, surfaceTop, surfaceLeft + surfaceWidth, surfaceTop + surfaceHeight};
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIFactory2> factory2;
  ComPtr<IDXGISwapChain1> swapChain1;
  HRESULT dxgiResult = device.As(&dxgiDevice);
  HRESULT factory2Result = factory.As(&factory2);
  if (FAILED(dxgiResult) || FAILED(factory2Result)) { LogNative("QueryDCompInterfaces", FAILED(dxgiResult) ? dxgiResult : factory2Result); SignalInitialization(glass, false); return false; }
  ComPtr<IDXGIDevice3> trimDevice;  // optional; Windows 8.1+
  dxgiDevice.As(&trimDevice);
  DXGI_SWAP_CHAIN_DESC1 swapDescription = {};
  RefreshLiveLens(glass);
  const auto bufferSize = [](int size) { return ((size + 48 + 63) / 64) * 64; };
  int bufferWidth = bufferSize(glass->lensWidth);
  int bufferHeight = bufferSize(glass->lensHeight);
  swapDescription.Width = static_cast<UINT>(bufferWidth);
  swapDescription.Height = static_cast<UINT>(bufferHeight);
  swapDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapDescription.SampleDesc.Count = 1;
  swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDescription.BufferCount = 2;
  swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  swapDescription.Scaling = DXGI_SCALING_STRETCH;
  swapDescription.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  HRESULT swapResult = factory2->CreateSwapChainForComposition(device.Get(), &swapDescription, nullptr, swapChain1.GetAddressOf());
  if (FAILED(swapResult)) { LogNative("CreateSwapChainForComposition", swapResult); SignalInitialization(glass, false); return false; }
  LogNative("CreateSwapChainForComposition");
  ComPtr<IDXGISwapChain3> swapChain;
  ComPtr<IDXGISwapChain2> pacedSwapChain;
  ComPtr<IDCompositionDevice> compositionDevice;
  ComPtr<IDCompositionTarget> compositionTarget;
  ComPtr<IDCompositionVisual> compositionVisual;
  ComPtr<IDCompositionEffectGroup> opacityEffect;
  HRESULT swap3Result = swapChain1.As(&swapChain);
  HRESULT swap2Result = swapChain1.As(&pacedSwapChain);
  HRESULT latencyResult = swap2Result == S_OK ? pacedSwapChain->SetMaximumFrameLatency(1) : swap2Result;
  HRESULT compositionResult = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(compositionDevice.GetAddressOf()));
  HRESULT targetResult = compositionResult == S_OK ? compositionDevice->CreateTargetForHwnd(glass->surface, TRUE, compositionTarget.GetAddressOf()) : compositionResult;
  HRESULT visualResult = targetResult == S_OK ? compositionDevice->CreateVisual(compositionVisual.GetAddressOf()) : targetResult;
  HRESULT effectResult = visualResult == S_OK ? compositionDevice->CreateEffectGroup(opacityEffect.GetAddressOf()) : visualResult;
  HRESULT contentResult = effectResult == S_OK ? compositionVisual->SetContent(swapChain.Get()) : effectResult;
  HRESULT rootResult = contentResult == S_OK ? compositionTarget->SetRoot(compositionVisual.Get()) : contentResult;
  HRESULT commitResult = rootResult == S_OK ? compositionDevice->Commit() : rootResult;
  if (FAILED(swap3Result) || FAILED(latencyResult) || FAILED(compositionResult) || FAILED(targetResult) || FAILED(visualResult) || FAILED(contentResult) || FAILED(rootResult) || FAILED(commitResult)) {
    LogNative("DCompInit", FAILED(swap3Result) ? swap3Result : FAILED(latencyResult) ? latencyResult : FAILED(compositionResult) ? compositionResult : FAILED(targetResult) ? targetResult : FAILED(visualResult) ? visualResult : FAILED(contentResult) ? contentResult : FAILED(rootResult) ? rootResult : commitResult);
    SignalInitialization(glass, false); return false;
  }
  LogNative("DCompInit");
  ComPtr<ID3D11VertexShader> vertexShader;
  ComPtr<ID3D11PixelShader> pixelShader;
  ComPtr<ID3D11PixelShader> frostedShader;
  ComPtr<ID3D11BlendState> overlayBlend;
  // V7 content layer resources (render worker only).
  ComPtr<ID3D11PixelShader> contentShader;
  ComPtr<ID3D11Buffer> contentConstants;  // b1: x = content alpha multiplier
  ComPtr<ID3D11Texture2D> contentTexture;
  ComPtr<ID3D11ShaderResourceView> contentView;
  unsigned contentUploaded = ~0u;
  ComPtr<ID3D11Buffer> constantBuffer;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11RasterizerState> rasterizer;
  namespace shaders = lume_material_bytecode;
  if (FAILED(device->CreateVertexShader(shaders::vertex, sizeof(shaders::vertex), nullptr, vertexShader.GetAddressOf())) ||
      FAILED(device->CreatePixelShader(shaders::liquid, sizeof(shaders::liquid), nullptr, pixelShader.GetAddressOf()))) {
    LogNative("ShaderInit", E_FAIL); SignalInitialization(glass, false); return false;
  }
  D3D11_BLEND_DESC blendDescription{};
  auto& blend = blendDescription.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = blend.SrcBlendAlpha = D3D11_BLEND_ONE;
  blend.DestBlend = blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(device->CreatePixelShader(shaders::frosted, sizeof(shaders::frosted), nullptr, frostedShader.GetAddressOf())) ||
      FAILED(device->CreatePixelShader(shaders::contentBlit, sizeof(shaders::contentBlit), nullptr, contentShader.GetAddressOf())) ||
      FAILED(device->CreateBlendState(&blendDescription, overlayBlend.GetAddressOf()))) {
    LogNative("FrostedShaderInit", E_FAIL); SignalInitialization(glass, false); return false;
  }
  LogNative("ShaderInit");
  D3D11_BUFFER_DESC constantDescription = {};
  constantDescription.ByteWidth = sizeof(GlassConstants);
  constantDescription.Usage = D3D11_USAGE_DYNAMIC;
  constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  D3D11_SAMPLER_DESC samplerDescription = {};
  samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
  D3D11_RASTERIZER_DESC rasterizerDescription = {};
  rasterizerDescription.FillMode = D3D11_FILL_SOLID;
  rasterizerDescription.CullMode = D3D11_CULL_NONE;
  rasterizerDescription.ScissorEnable = TRUE;
  rasterizerDescription.DepthClipEnable = TRUE;
  const HRESULT bufferResult = device->CreateBuffer(&constantDescription, nullptr, constantBuffer.GetAddressOf());
  const HRESULT samplerResult = device->CreateSamplerState(&samplerDescription, sampler.GetAddressOf());
  const HRESULT rasterResult = device->CreateRasterizerState(&rasterizerDescription, rasterizer.GetAddressOf());
  if (FAILED(bufferResult) || FAILED(samplerResult) || FAILED(rasterResult)) {
    LogNative("PipelineResources", FAILED(bufferResult) ? bufferResult : FAILED(samplerResult) ? samplerResult : rasterResult);
    SignalInitialization(glass, false); return false;
  }
  // D3D11 automatically rotates buffer zero after Present; nonzero buffers
  // cannot be opened as render targets (unlike a D3D12 swap chain).
  ComPtr<ID3D11Texture2D> backbuffer;
  ComPtr<ID3D11RenderTargetView> renderTarget;
  ComPtr<ID3D11Texture2D> surfaceTexture;
  ComPtr<ID3D11ShaderResourceView> surfaceView;
  {
    const HRESULT backResult = swapChain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
    const HRESULT viewResult = SUCCEEDED(backResult) ? device->CreateRenderTargetView(backbuffer.Get(), nullptr, renderTarget.GetAddressOf()) : backResult;
    if (FAILED(viewResult)) {
      LogNative("BackBuffer", viewResult);
      SignalInitialization(glass, false); return false;
    }
    D3D11_TEXTURE2D_DESC feedDesc = {};
    feedDesc.Width = static_cast<UINT>(bufferWidth); feedDesc.Height = static_cast<UINT>(bufferHeight);
    feedDesc.ArraySize = feedDesc.MipLevels = 1; feedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    feedDesc.SampleDesc.Count = 1; feedDesc.Usage = D3D11_USAGE_DEFAULT;
    feedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const HRESULT feedTexture = device->CreateTexture2D(&feedDesc, nullptr, surfaceTexture.GetAddressOf());
    const HRESULT feedView = SUCCEEDED(feedTexture)
        ? device->CreateShaderResourceView(surfaceTexture.Get(), nullptr, surfaceView.GetAddressOf()) : feedTexture;
    if (FAILED(feedView)) {
      LogNative("SurfaceFeed", feedView);
      SignalInitialization(glass, false); return false;
    }
  }
  HANDLE frameLatencyWaitableObject = pacedSwapChain->GetFrameLatencyWaitableObject();
  if (!frameLatencyWaitableObject || frameLatencyWaitableObject == INVALID_HANDLE_VALUE) {
    SignalInitialization(glass, false); return false;
  }
  struct FrameLatencyLease { HANDLE value; ~FrameLatencyLease(){CloseHandle(value);} } latencyLease{frameLatencyWaitableObject};
  LogNative("ResourceInit");
  RefreshLiveLens(glass);
  SignalInitialization(glass, true);
  RECT previousRegion{}, publishedRegion{};
  bool regionExact = false;
  float previousInnerRadius=-1;
  POINT presentedOrigin{}, renderOrigin{};
  float presentedOpacity = -1;
  unsigned presentedFrames = 0;
  unsigned renderedRevision = 0;
  bool wasVisible = false;
  ULONGLONG motionUntil = 0;
  HMONITOR presentationMonitor = nullptr;
  ComPtr<IDXGIOutput> presentationOutput;
  auto interactionTime = std::chrono::steady_clock::now();
  int interactionX = glass->lensX, interactionY = glass->lensY;
  bool interactionDirty = false;
  // LUME_GLASS_TIMING (with LUME_GLASS_LOG): one line per presented frame, for
  // frame-interval probes (launchpad_open_probe, island_expand_probe).
  const bool frameTiming = GetEnvironmentVariableA("LUME_GLASS_TIMING", nullptr, 0) > 0;
  // Frame sync with the owner (see the loop top).
  bool syncPending = false;
  LARGE_INTEGER qpcFrequency{};
  QueryPerformanceFrequency(&qpcFrequency);
  int64_t vblankQpc = 0;
  const HANDLE syncTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  struct SyncTimerLease { HANDLE value; ~SyncTimerLease() { if (value) CloseHandle(value); } } syncTimerLease{syncTimer};
  unsigned revisionAtVblank = 0;
  double refreshMs = 1000.0 / 60;
  auto vblankAt = std::chrono::steady_clock::now();
  while (!glass->stop.load(std::memory_order_relaxed)) {
    ++glass->sceneChecks;
    const HRESULT deviceState=device->GetDeviceRemovedReason();
    if (FAILED(deviceState)) {glass->lastError=deviceState;glass->failed=true;break;}
    if (!IsWindow(glass->owner)) { glass->stop = true; break; }
    if (IsWindowVisible(glass->owner) && !IsIconic(glass->owner)) wasVisible = true;
    else if (wasVisible) break;
    if (GetSystemMetrics(SM_XVIRTUALSCREEN) != surfaceLeft || GetSystemMetrics(SM_YVIRTUALSCREEN) != surfaceTop ||
        GetSystemMetrics(SM_CXVIRTUALSCREEN) != surfaceWidth || GetSystemMetrics(SM_CYVIRTUALSCREEN) != surfaceHeight) break;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message); DispatchMessageW(&message);
    }
    // V7 timeline: place this frame where the owner's motion is at the moment
    // the frame reaches the screen (the next refresh), whatever its thread does.
    bool timelineActive = false;
    {
      std::lock_guard<std::mutex> lock(glass->contentMutex);
      if (!glass->timeline.empty() && glass->timelineStep > 0) {
        LARGE_INTEGER nowQpc{};
        QueryPerformanceCounter(&nowQpc);
        const double ticksPerMs = static_cast<double>(qpcFrequency.QuadPart) / 1000.0;
        double display = static_cast<double>(nowQpc.QuadPart);
        if (vblankQpc) {
          const double period = refreshMs * ticksPerMs;
          const double since = display - static_cast<double>(vblankQpc);
          display = static_cast<double>(vblankQpc) + period * (std::max)(1.0, std::ceil(since / period));
        }
        const double t = (display - static_cast<double>(glass->timelineStart)) / ticksPerMs / glass->timelineStep;
        const size_t last = glass->timeline.size() - 1;
        const double clamped = (std::max)(0.0, (std::min)(static_cast<double>(last), t));
        const size_t index = static_cast<size_t>(clamped);
        const auto& a = glass->timeline[index];
        const auto& b = glass->timeline[(std::min)(last, index + 1)];
        const float f = static_cast<float>(clamped - static_cast<double>(index));
        const auto mix = [f](float x, float y) { return x + (y - x) * f; };
        glass->lensOverride = true;
        glass->lensRect[0] = mix(a.lensX, b.lensX); glass->lensRect[1] = mix(a.lensY, b.lensY);
        glass->lensRect[2] = mix(a.lensWidth, b.lensWidth); glass->lensRect[3] = mix(a.lensHeight, b.lensHeight);
        glass->contentRect[0] = mix(a.contentX, b.contentX); glass->contentRect[1] = mix(a.contentY, b.contentY);
        glass->contentRect[2] = mix(a.contentWidth, b.contentWidth); glass->contentRect[3] = mix(a.contentHeight, b.contentHeight);
        glass->opacity = mix(a.opacity, b.opacity);
        glass->contentOpacity = mix(a.contentOpacity, b.contentOpacity);
        glass->cornerRadius = mix(a.cornerRadius, b.cornerRadius);
        {
          std::lock_guard<std::mutex> panelsLock(glass->discsMutex);
          glass->panels.clear();
          for (uint32_t i = 0; i < a.panelCount && i < b.panelCount; ++i) {
            const auto& p = a.panels[i];
            const auto& q = b.panels[i];
            glass->panels.push_back({mix(p.x, q.x), mix(p.y, q.y), mix(p.width, q.width), mix(p.height, q.height),
                p.radius < 0 ? p.radius : mix(p.radius, q.radius)});
          }
        }
        // Keep presenting every refresh until a refresh past the last frame.
        timelineActive = t <= static_cast<double>(last) + 1.0 / glass->timelineStep * refreshMs + 1;
      }
    }
    if (timelineActive) { motionUntil = GetTickCount64() + 120; interactionDirty = true; syncPending = false; }
    // While the owner animates it resizes its window and then submits the
    // material, once per refresh. Wait (at most half a refresh) for this
    // frame's submission before reading the lens, so glass and foreground
    // reach the screen on the same refresh instead of drifting by one.
    if (syncPending && GetTickCount64() < motionUntil) {
      const auto deadline = vblankAt + std::chrono::duration<double, std::milli>(refreshMs / 2);
      for (auto now = std::chrono::steady_clock::now();
           glass->revision.load() == revisionAtVblank && now < deadline; now = std::chrono::steady_clock::now()) {
        const auto left = std::chrono::duration<double, std::milli>(deadline - now).count();
        // A sub-millisecond timer: a plain wait rounds up to the 15.6 ms system
        // tick and would miss the very frame this wait is meant to keep.
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(left * 10000);
        if (syncTimer && SetWaitableTimer(syncTimer, &due, 0, nullptr, nullptr, FALSE)) {
          const HANDLE handles[2] = {glass->wake->Handle(), syncTimer};
          WaitForMultipleObjects(2, handles, FALSE, 50);
        } else {
          Sleep(0);
        }
      }
    }
    syncPending = false;
    RefreshLiveLens(glass);
    const auto lensRead = std::chrono::steady_clock::now();
    const auto interactionNow = std::chrono::steady_clock::now();
    lume_glass::InteractionInput input;
    {
      std::lock_guard<std::mutex> lock(glass->interactionMutex);
      input = glass->interactionInput;
    }
    if (glass->frosted) input = {};
    const int nextInteractionX = glass->lensX, nextInteractionY = glass->lensY;
    interactionDirty |= glass->interaction.Step(input, std::chrono::duration<float>(interactionNow - interactionTime).count(),
        static_cast<float>(nextInteractionX - interactionX), static_cast<float>(nextInteractionY - interactionY),
        GetDpiForWindow(glass->owner) / 96.f);
    interactionTime = interactionNow; interactionX = nextInteractionX; interactionY = nextInteractionY;
    glass->interactionPressure = glass->interaction.pressure.value;
    glass->interactionMotion = std::hypot(glass->interaction.motionX.value, glass->interaction.motionY.value);
    const auto monitor = MonitorFromWindow(glass->owner, MONITOR_DEFAULTTONEAREST);
    if (monitor != presentationMonitor) {
      presentationMonitor = monitor;
      presentationOutput.Reset();
      for (UINT index = 0; ; ++index) {
        ComPtr<IDXGIOutput> candidate;
        if (FAILED(adapter->EnumOutputs(index, candidate.GetAddressOf()))) break;
        DXGI_OUTPUT_DESC description{};
        if (SUCCEEDED(candidate->GetDesc(&description)) && description.Monitor == monitor) {
          presentationOutput = std::move(candidate);
          DEVMODEW mode{}; mode.dmSize = sizeof(mode);
          refreshMs = EnumDisplaySettingsW(description.DeviceName, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
              ? 1000.0 / mode.dmDisplayFrequency : 1000.0 / 60;
          break;
        }
      }
    }
    RECT currentRegion = {glass->lensX - surfaceLeft - 24, glass->lensY - surfaceTop - 24,
        glass->lensX + glass->lensWidth - surfaceLeft + 24, glass->lensY + glass->lensHeight - surfaceTop + 24};
    const float innerRadius=glass->innerRadius.load(std::memory_order_relaxed);
    const bool geometryChanged = !EqualRect(&currentRegion, &previousRegion) || innerRadius!=previousInnerRadius;
    if (geometryChanged) motionUntil = GetTickCount64() + 120;
    // A plain lens publishes its window region exactly only at rest. While it
    // moves or resizes the region only grows (with headroom): a new region
    // every frame cost 1-5 ms and made the glass miss vblanks. Outside the
    // lens the swap chain is transparent, so a larger region shows nothing.
    const bool plain = innerRadius <= 0;
    RECT regionTarget = currentRegion;
    bool publish = false;
    if (!plain) {
      publish = geometryChanged;
    } else if (geometryChanged && presentedFrames > 0 && !IsRectEmpty(&publishedRegion)) {
      RECT grown{};
      UnionRect(&grown, &publishedRegion, &currentRegion);
      if (!EqualRect(&grown, &publishedRegion)) {
        InflateRect(&grown, (currentRegion.right - currentRegion.left) / 8, (currentRegion.bottom - currentRegion.top) / 8);
        regionTarget = grown;
        publish = true;
      }
    } else if (geometryChanged) {
      publish = true;  // first frame
    } else if (!regionExact && GetTickCount64() >= motionUntil) {
      publish = true;  // settled: the exact region again
    }
    if (publish) {
      HRGN region = CreateRectRgn(regionTarget.left, regionTarget.top, regionTarget.right, regionTarget.bottom);
      if(!plain){
        DeleteObject(region);
        const float ownerDpi=GetDpiForWindow(glass->owner)/96.0f;
        const int left=glass->lensX-surfaceLeft,top=glass->lensY-surfaceTop;
        const int width=glass->lensWidth,height=glass->lensHeight;
        const int edge=static_cast<int>(std::lround(8*ownerDpi));
        const int hole=static_cast<int>(std::lround(innerRadius*ownerDpi));
        region=lume::platform::windows::CreateAnnularRegion(
            {left+edge,top+edge,left+width-edge,top+height-edge},
            {left+width/2-hole,top+height/2-hole,left+width/2+hole,top+height/2+hole});
      }
      if (!lume::platform::windows::PublishRegion(glass->surface,region)) {
        glass->lastError=E_FAIL;glass->frameReady=false;
        ShowWindow(glass->surface,SW_HIDE);break;
      }
      publishedRegion = regionTarget;
      regionExact = EqualRect(&regionTarget, &currentRegion) != FALSE;
    }
    if (geometryChanged) {
      previousRegion = currentRegion;
      previousInnerRadius = innerRadius;
    }
    std::unique_lock<std::mutex> contextLock(graphics->contextMutex);
    // Clear bound resources on early exits too. The shared context must not
    // retain this scene's swap-chain targets after its worker releases them.
    struct ClearBindingsOnExit {
      ID3D11DeviceContext* context;
      std::unique_lock<std::mutex>& lock;
      ~ClearBindingsOnExit() { if (lock.owns_lock()) context->ClearState(); }
    } clearBindings{context.Get(), contextLock};
    // Buffers follow the lens exactly only at rest. While it moves they only
    // grow, with headroom, so a resizing lens (an opening launchpad, a
    // spreading island) does not reallocate the swap chain every 64 px.
    const bool lensMoving = GetTickCount64() < motionUntil;
    const auto nextSize = [&](int lens, int current, int limit) {
      const int exact = bufferSize(lens);
      if (exact > current) return lensMoving ? (std::min)((std::max)(exact, bufferSize(lens + lens / 4)), bufferSize(limit)) : exact;
      return lensMoving ? current : exact;
    };
    // An explicit lens moves within its owner (with overshoot): size the
    // buffers for that whole stage at once rather than growing mid-motion.
    const int stageWidth = glass->stageWidth.load(std::memory_order_relaxed);
    const int stageHeight = glass->stageHeight.load(std::memory_order_relaxed);
    const int nextWidth = stageWidth
        ? (std::max)(bufferWidth, bufferSize(stageWidth + stageWidth / 5)) : nextSize(glass->lensWidth, bufferWidth, surfaceWidth);
    const int nextHeight = stageWidth
        ? (std::max)(bufferHeight, bufferSize(stageHeight + stageHeight / 5)) : nextSize(glass->lensHeight, bufferHeight, surfaceHeight);
    if (nextWidth != bufferWidth || nextHeight != bufferHeight) {
      context->OMSetRenderTargets(0, nullptr, nullptr);
      renderTarget.Reset();
      surfaceTexture.Reset();
      surfaceView.Reset();
      backbuffer.Reset();
      const HRESULT resized = swapChain->ResizeBuffers(2, nextWidth, nextHeight,
          DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
      if (FAILED(resized)) { glass->lastError = resized; break; }
      const HRESULT acquired = swapChain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
      const HRESULT recreated = SUCCEEDED(acquired)
          ? device->CreateRenderTargetView(backbuffer.Get(), nullptr, renderTarget.GetAddressOf()) : acquired;
      const HRESULT shaderView = SUCCEEDED(recreated)
          ? S_OK : recreated;
      D3D11_TEXTURE2D_DESC feedDesc = {};
      feedDesc.Width = static_cast<UINT>(nextWidth); feedDesc.Height = static_cast<UINT>(nextHeight);
      feedDesc.ArraySize = feedDesc.MipLevels = 1; feedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      feedDesc.SampleDesc.Count = 1; feedDesc.Usage = D3D11_USAGE_DEFAULT;
      feedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      const HRESULT feedTexture = SUCCEEDED(shaderView)
          ? device->CreateTexture2D(&feedDesc, nullptr, surfaceTexture.GetAddressOf()) : shaderView;
      const HRESULT feedView = SUCCEEDED(feedTexture)
          ? device->CreateShaderResourceView(surfaceTexture.Get(), nullptr, surfaceView.GetAddressOf()) : feedTexture;
      if (FAILED(recreated) || FAILED(feedView)) { glass->lastError = FAILED(recreated) ? recreated : feedView; break; }
      bufferWidth = nextWidth; bufferHeight = nextHeight;
      presentedFrames = 0;
    }
    glass->surfacePixels = bufferWidth * bufferHeight;
    // The image (Present) and the visual offset (Commit) reach DWM as two
    // separate updates. Moving the origin with the lens every frame let DWM show
    // a new image at the old offset now and then: the whole glass jolted by that
    // frame's motion and back. An explicit lens draws on a fixed origin centred
    // on its stage, so the offset holds still through the motion.
    renderOrigin = {currentRegion.left, currentRegion.top};
    if (stageWidth) {
      const POINT stage{glass->stageX.load(std::memory_order_relaxed) - surfaceLeft - (bufferWidth - stageWidth) / 2,
                        glass->stageY.load(std::memory_order_relaxed) - surfaceTop - (bufferHeight - stageHeight) / 2};
      if (currentRegion.left >= stage.x && currentRegion.top >= stage.y &&
          currentRegion.right <= stage.x + bufferWidth && currentRegion.bottom <= stage.y + bufferHeight)
        renderOrigin = stage;  // otherwise (outside its stage) follow the lens
    }
    const int renderLeft = surfaceLeft + renderOrigin.x;
    const int renderTop = surfaceTop + renderOrigin.y;
    ID3D11RenderTargetView* target = renderTarget.Get();
    D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(bufferWidth), static_cast<float>(bufferHeight), 0, 1};
    const auto bindPipeline = [&] {
      context->OMSetRenderTargets(1, &target, nullptr);
      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
      context->RSSetState(rasterizer.Get());
      context->RSSetViewports(1, &viewport);
      context->IASetInputLayout(nullptr);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertexShader.Get(), nullptr, 0);
      context->PSSetShader(pixelShader.Get(), nullptr, 0);
      context->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    };
    bindPipeline();
    const float dpi = GetDpiForWindow(glass->owner) / 96.0f;
    const float materialZoom = (std::max)(1.0f, (std::min)(glass->lensWidth / (340.0f * dpi), glass->lensHeight / (360.0f * dpi)));
    // The reflection reaches 24px beyond the rim with a 7.41px blur footprint.
    const int captureMargin = static_cast<int>(std::ceil(32.0f * dpi * materialZoom)) + 1;
    RECT region = {glass->lensX.load() - captureMargin, glass->lensY.load() - captureMargin,
        glass->lensX.load() + glass->lensWidth.load() + captureMargin,
        glass->lensY.load() + glass->lensHeight.load() + captureMargin};
    // An explicit lens animates inside its owner: sample the whole window plus
    // room for the overshoot once, so the scene does not re-render (and pick up
    // new sources) on every frame of the motion.
    if (std::lock_guard<std::mutex> lock(glass->contentMutex); glass->lensOverride) {
      RECT client{};
      POINT origin{};
      if (GetClientRect(glass->owner, &client) && ClientToScreen(glass->owner, &origin)) {
        const int width = client.right - client.left, height = client.bottom - client.top;
        const float zoom = (std::max)(1.0f, (std::min)(width / (340.0f * dpi), height / (360.0f * dpi)));
        const int margin = static_cast<int>(std::ceil(32.0f * dpi * zoom)) + 1;
        const RECT stage = {origin.x - width / 10 - margin, origin.y - height / 10 - margin,
            origin.x + width + width / 10 + margin, origin.y + height + height / 10 + margin};
        UnionRect(&region, &region, &stage);
      }
    }
    const RECT sampleBounds = OwnerMonitorBounds(glass->owner,
        {surfaceLeft, surfaceTop, surfaceLeft + surfaceWidth, surfaceTop + surfaceHeight});
    RECT monitorRegion{};
    bool sceneChanged = false;
    const HWND anchor=glass->sceneAnchor.load(std::memory_order_relaxed);
    // Only the reserved wide-blur profile 4 uses the reduced backdrop; every
    // other profile (including the liquid path) needs the full-resolution scene.
    scene.SetBackdropScale(glass->opticalProfile.load(std::memory_order_relaxed) == 4 ? 4 : 1);
    if (IntersectRect(&monitorRegion, &region, &sampleBounds))
      sceneChanged = scene.Render(anchor&&IsWindow(anchor)?anchor:glass->owner,
          glass->surface, monitorRegion, device.Get(), context.Get());
    output.desktopView = scene.View();
    output.bounds = scene.PresentationBounds(monitorRegion);
    glass->scenePixels = scene.Pixels();
    glass->sources = scene.sourceCount;
    glass->readySources = scene.readyCount;
    glass->desktopIconSources = scene.desktopIconSources;
    glass->desktopIconReady = scene.desktopIconReady;
    glass->captureFrames = scene.captureFrames;
    glass->deferredScenes = scene.deferredScenes;
    if (glass->sampleRequested.exchange(false)) {
      glass->sampleColor = scene.Sample(device.Get(), context.Get(), glass->lensX + glass->lensWidth / 2,
          glass->lensY + glass->lensHeight / 2);
      ++glass->samples;
    }
    // The shared immediate context is only held while issuing GPU work. Frame
    // pacing and message waits must leave it available to other glass scenes.
    context->ClearState();
    if (scene.TakeClosedCaptures()) {
      // Full-window captures of windows that passed under the glass were just
      // closed. D3D destroys their textures only on a flush, and the driver
      // keeps the freed memory pooled until trimmed; a static lens may not
      // present for minutes, so without this every such window leaks GPU memory.
      context->Flush();
      if (trimDevice) trimDevice->Trim();
    }
    contextLock.unlock();
    const unsigned revision = glass->revision.load();
    if (presentedFrames >= 2 && GetTickCount64() >= motionUntil && !sceneChanged && !interactionDirty && revision == renderedRevision) {
      // Capture frames and window events wake each consumer independently.
      // Keep a bounded fallback for missed shell events and capture retries.
      // Do not consume the swap-chain semaphore until a frame is needed.
      const HANDLE wakeEvent = glass->wake->Handle();
      MsgWaitForMultipleObjectsEx(1, &wakeEvent, windowEvents.Available() ? 250 : kIdleWakeFallbackMs,
          QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      // A new press after an idle wait starts now, not 250ms in the past.
      interactionTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(8);
      continue;
    }
    // Messages (window events from anything on the desktop, the owner's own
    // moves) arrive several times per frame. Handle them and keep waiting for
    // the frame slot: restarting the loop on each one made the glass miss
    // frames whenever something else on the desktop was animating.
    const ULONGLONG waitEnd = GetTickCount64() + 1000;
    DWORD wait = WAIT_TIMEOUT;
    for (;;) {
      const ULONGLONG now = GetTickCount64();
      wait = now >= waitEnd ? WAIT_TIMEOUT : MsgWaitForMultipleObjectsEx(1, &frameLatencyWaitableObject,
          static_cast<DWORD>(waitEnd - now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      if (wait != WAIT_OBJECT_0 + 1) break;
      MSG pending{};
      while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&pending); DispatchMessageW(&pending); }
      if (glass->stop.load(std::memory_order_relaxed)) break;
    }
    if (glass->stop.load(std::memory_order_relaxed)) break;
    if (wait == WAIT_TIMEOUT) { ++glass->revision; continue; }
    if (wait != WAIT_OBJECT_0) break;
    contextLock.lock();
    // Other workers can bind their own constant buffers, samplers and shaders
    // while this worker waits. Restore the entire pipeline, not only its target.
    bindPipeline();
    const float transparent[4] = {};
    context->ClearRenderTargetView(target, transparent);
    context->PSSetShader(glass->frosted ? frostedShader.Get() : pixelShader.Get(), nullptr, 0);
    if (!DrawLens(glass, context.Get(), output, constantBuffer.Get(), renderLeft, renderTop, bufferWidth, bufferHeight,
                  sampleBounds)) break;
    std::vector<GlassOverlay> discs;
    {
      std::lock_guard<std::mutex> lock(glass->discsMutex);
      discs = glass->discs;
      discs.insert(discs.end(), glass->panels.begin(), glass->panels.end());
    }
    context->PSSetShader(frostedShader.Get(), nullptr, 0);
    context->OMSetBlendState(overlayBlend.Get(), nullptr, 0xffffffff);
    bool discsDrawn = true;
    for (const auto& disc : discs) {
      if (!DrawLens(glass, context.Get(), output, constantBuffer.Get(), renderLeft, renderTop, bufferWidth, bufferHeight,
                    sampleBounds, &disc)) {
        discsDrawn = false; break;
      }
    }
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    if (!discsDrawn) break;
    // V7 content layer: the owner's foreground drawn over the glass in this
    // same present, scaled on the GPU (mipmapped, so a shrunken panel stays
    // smooth instead of shimmering).
    {
      std::unique_lock<std::mutex> contentLock(glass->contentMutex);
      if (glass->contentEnabled && glass->contentPixelWidth && glass->contentPixelHeight) {
        if (!contentView || glass->contentRevision != contentUploaded) {
          contentView.Reset();
          contentTexture.Reset();
          D3D11_TEXTURE2D_DESC contentDesc{};
          contentDesc.Width = glass->contentPixelWidth;
          contentDesc.Height = glass->contentPixelHeight;
          contentDesc.MipLevels = 0;
          contentDesc.ArraySize = 1;
          contentDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
          contentDesc.SampleDesc.Count = 1;
          contentDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
          contentDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
          if (SUCCEEDED(device->CreateTexture2D(&contentDesc, nullptr, contentTexture.GetAddressOf())) &&
              SUCCEEDED(device->CreateShaderResourceView(contentTexture.Get(), nullptr, contentView.GetAddressOf()))) {
            context->UpdateSubresource(contentTexture.Get(), 0, nullptr, glass->contentPixels.data(),
                glass->contentPixelWidth * 4, 0);
            context->GenerateMips(contentView.Get());
            contentUploaded = glass->contentRevision;
          } else {
            contentView.Reset();
            contentTexture.Reset();
          }
        }
        const float contentScale = GetDpiForWindow(glass->owner) / 96.0f;
        const bool precise = glass->lensPrecise.load();
        const float contentLeft = (precise ? glass->lensXf.load() : static_cast<float>(glass->lensX.load())) +
            glass->contentRect[0] * contentScale - renderLeft;
        const float contentTop = (precise ? glass->lensYf.load() : static_cast<float>(glass->lensY.load())) +
            glass->contentRect[1] * contentScale - renderTop;
        const float contentWidth = glass->contentRect[2] * contentScale;
        const float contentHeight = glass->contentRect[3] * contentScale;
        // The visual's opacity scales the whole frame; the content's own
        // opacity is net, so divide that back out.
        const float frameOpacity = glass->opacity.load();
        const float contentAlpha = frameOpacity > .001f ? (std::min)(1.f, glass->contentOpacity / frameOpacity) : 0.f;
        contentLock.unlock();
        if (!contentConstants) {
          D3D11_BUFFER_DESC description = {};
          description.ByteWidth = 16;
          description.Usage = D3D11_USAGE_DYNAMIC;
          description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
          description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
          device->CreateBuffer(&description, nullptr, contentConstants.GetAddressOf());
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const bool alphaSet = contentConstants && SUCCEEDED(context->Map(contentConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        if (alphaSet) {
          const float values[4] = {contentAlpha, 0, 0, 0};
          std::memcpy(mapped.pData, values, sizeof(values));
          context->Unmap(contentConstants.Get(), 0);
        }
        if (alphaSet && contentAlpha > 0 && contentView && contentWidth >= 1 && contentHeight >= 1) {
          const D3D11_VIEWPORT contentViewport = {contentLeft, contentTop, contentWidth, contentHeight, 0, 1};
          const D3D11_RECT contentScissor = {0, 0, bufferWidth, bufferHeight};
          context->RSSetViewports(1, &contentViewport);
          context->RSSetScissorRects(1, &contentScissor);
          context->OMSetBlendState(overlayBlend.Get(), nullptr, 0xffffffff);
          context->PSSetShader(contentShader.Get(), nullptr, 0);
          context->PSSetConstantBuffers(1, 1, contentConstants.GetAddressOf());
          context->PSSetShaderResources(0, 1, contentView.GetAddressOf());
          context->Draw(3, 0);
          ID3D11ShaderResourceView* none = nullptr;
          context->PSSetShaderResources(0, 1, &none);
          context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
          context->RSSetViewports(1, &viewport);
        }
      } else if (contentView) {
        contentView.Reset();
        contentTexture.Reset();
      }
    }
    if (surfaceTexture && surfaceView) {
      context->OMSetRenderTargets(0, nullptr, nullptr);
      context->CopyResource(surfaceTexture.Get(), backbuffer.Get());
      const RECT feedBounds = {renderLeft, renderTop, renderLeft + bufferWidth, renderTop + bufferHeight};
      g_glassCaptureRegistry.PublishSurface(glass->surface, feedBounds, surfaceView.Get());
    }
    if (glass->outputSampleRequested.exchange(false)) {
      // The native surface is excluded from Windows screen capture. Unbind
      // the render target before copying one diagnostic pixel from the actual
      // swap-chain output so material tests do not mistake the raw desktop
      // scene for the rendered glass.
      context->OMSetRenderTargets(0, nullptr, nullptr);
      const int sampleX = (std::max)(0, (std::min)(bufferWidth - 1,
          glass->lensX.load() - renderLeft + glass->lensWidth.load() / 2));
      const int sampleY = (std::max)(0, (std::min)(bufferHeight - 1,
          glass->lensY.load() - renderTop + glass->lensHeight.load() / 2));
      glass->outputSampleColor = SampleTexturePixel(device.Get(), context.Get(), backbuffer.Get(), sampleX, sampleY);
      ++glass->outputSamples;
    }
    glass->sources = scene.sourceCount;
    glass->readySources = scene.readyCount;
    glass->desktopIconSources = scene.desktopIconSources;
    glass->desktopIconReady = scene.desktopIconReady;
    glass->captureFrames = scene.captureFrames;
    // Submit without a per-chain vblank interval, then wait for the output below.
    // Multiple synchronized chains on one device otherwise halve the cadence.
    const HRESULT presented = swapChain->Present(0, 0);
    glass->lastError = presented;
    if (FAILED(presented)) break;
    const float opacity = glass->opacity.load();
    if (renderOrigin.x != presentedOrigin.x || renderOrigin.y != presentedOrigin.y || presentedFrames == 0 || opacity != presentedOpacity) {
      compositionVisual->SetOffsetX(static_cast<float>(renderOrigin.x));
      compositionVisual->SetOffsetY(static_cast<float>(renderOrigin.y));
      opacityEffect->SetOpacity(opacity);
      compositionVisual->SetEffect(opacity < 1.f ? opacityEffect.Get() : nullptr);
      const HRESULT committed = compositionDevice->Commit();
      if (FAILED(committed)) { glass->lastError = committed; break; }
      presentedOrigin = renderOrigin;
      presentedOpacity = opacity;
    }
    ++presentedFrames;
    interactionDirty = false;
    renderedRevision = revision;
    glass->frameReady = scene.Presentable();
    context->ClearState();
    contextLock.unlock();
    // Present(0)'s latency handle can signal immediately for discarded frames.
    // Wait on the owner's display outside the shared lock to bound submissions
    // to its real refresh rate, including when the widget changes monitors.
    if (!presentationOutput || FAILED(presentationOutput->WaitForVBlank())) Sleep(16);
    vblankAt = std::chrono::steady_clock::now();
    { LARGE_INTEGER at{}; QueryPerformanceCounter(&at); vblankQpc = at.QuadPart; }
    revisionAtVblank = glass->revision.load();
    syncPending = true;
    if (frameTiming) {
      // Absolute steady-clock milliseconds, comparable with a probe's own samples.
      const auto absolute = [](std::chrono::steady_clock::time_point at) {
        return std::chrono::duration<double, std::milli>(at.time_since_epoch()).count();
      };
      char line[128]{};
      bool content = false; unsigned contentRev = 0;
      { std::lock_guard<std::mutex> lock(glass->contentMutex); content = glass->contentEnabled; contentRev = glass->contentRevision; }
      std::snprintf(line, sizeof(line), "T t=%.2f read=%.2f lens=%dx%d at=%d,%d content=%d rev=%u opacity=%.2f", absolute(std::chrono::steady_clock::now()),
          absolute(lensRead), glass->lensWidth.load(), glass->lensHeight.load(), glass->lensX.load(), glass->lensY.load(), content ? 1 : 0, contentRev, glass->opacity.load());
      LogNative(line);
    }
    if (glass->frameReady && IsWindowVisible(glass->owner)) {
      if (!IsWindowVisible(glass->surface) || GetWindow(glass->owner, GW_HWNDNEXT) != glass->surface)
        SetWindowPos(glass->surface, glass->owner, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    } else if (IsWindowVisible(glass->surface)) {
      ++glass->surfaceHides;
      ShowWindow(glass->surface, SW_HIDE);
    }
    if (++glass->frames == 120) {
      char stage[128]{};
      std::snprintf(stage, sizeof(stage), "Frames=120 Sources=%u Ready=%u DesktopIcons=%u/%u",
          scene.sourceCount, scene.readyCount, scene.desktopIconReady, scene.desktopIconSources);
      LogNative(stage);
    }
    // Publish only after the frame counter and visible surface are committed;
    // the UI reads Status immediately in response to this asynchronous message.
    NotifyStatus(glass);
  }
  return true;
}


bool Valid(const LumeMaterialParameters* p) {
  return p && (p->size==LumeMaterialParametersV1Size || p->size==LumeMaterialParametersV2Size ||
              p->size==LumeMaterialParametersV3Size || p->size==LumeMaterialParametersV4Size ||
              p->size==LumeMaterialParametersV5Size || p->size==LumeMaterialParametersV6Size ||
              p->size==((LumeMaterialParametersV6Size+7)&~7u) || p->size==sizeof(*p))
      && p->owner && IsWindow(p->owner) && p->opticalProfile<=4 && p->panelCount<=8
      && (p->size==LumeMaterialParametersV1Size || (std::isfinite(p->cornerRadius) && p->cornerRadius>=0))
      && (p->size<LumeMaterialParametersV3Size || !p->sceneAnchor || IsWindow(p->sceneAnchor))
      && (p->size<LumeMaterialParametersV4Size || p->bottomInset<=4096)
      && (p->size<LumeMaterialParametersV5Size || (std::isfinite(p->innerRadius) && p->innerRadius>=0 && p->innerRadius<=4096))
      && (p->size<LumeMaterialParametersV6Size || !p->opacityEnabled || (std::isfinite(p->opacity) && p->opacity>=0 && p->opacity<=1))
      && (p->size!=sizeof(*p) || !p->lensOverride || (std::isfinite(p->lensX) && std::isfinite(p->lensY) &&
          std::isfinite(p->lensWidth) && std::isfinite(p->lensHeight) && p->lensWidth>=1 && p->lensHeight>=1 &&
          p->lensWidth<=32768 && p->lensHeight<=32768))
      && (p->size!=sizeof(*p) || !p->contentEnabled || (p->contentPixels && p->contentPixelWidth>0 && p->contentPixelHeight>0 &&
          p->contentPixelWidth<=8192 && p->contentPixelHeight<=8192 && std::isfinite(p->contentX) && std::isfinite(p->contentY) &&
          std::isfinite(p->contentWidth) && std::isfinite(p->contentHeight) && p->contentWidth>=0 && p->contentHeight>=0))
      && (p->size!=sizeof(*p) || !p->timelineCount || (p->timeline && p->timelineCount<=8192 &&
          std::isfinite(p->timelineStepMs) && p->timelineStepMs>=.5f && p->timelineStepMs<=100));
}
// lensOverride 2: fixes the owner origin the timeline is relative to, and its
// whole extent in screen pixels as the stage (caller holds contentMutex).
void AnchorStage(GlassInstance& g) {
  POINT origin{0, 0};
  ClientToScreen(g.owner, &origin);
  const float scale = GetDpiForWindow(g.owner) / 96.0f;
  float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
  for (const auto& k : g.timeline) {
    left = (std::min)(left, k.lensX); top = (std::min)(top, k.lensY);
    right = (std::max)(right, k.lensX + k.lensWidth); bottom = (std::max)(bottom, k.lensY + k.lensHeight);
  }
  g.anchor = origin;
  g.anchorStage = {origin.x + static_cast<LONG>(std::floor(left * scale)), origin.y + static_cast<LONG>(std::floor(top * scale)),
                   origin.x + static_cast<LONG>(std::ceil(right * scale)), origin.y + static_cast<LONG>(std::ceil(bottom * scale))};
}
void Configure(GlassInstance& g,const LumeMaterialParameters& p) {
  // A running timeline drives the lens, content rect, opacity, radius and
  // panels per frame; a call made meanwhile (a hover repaint, the pointer
  // moving) must not reset them to its first frame, which blanked the panel.
  const bool timed=p.size==sizeof(p)&&p.timelineCount>0;
  g.frosted=p.frosted!=0;g.panelSurface=p.frosted==2;g.opticalProfile=p.opticalProfile;g.lightForeground=p.lightForeground!=0;
  if(!timed)g.cornerRadius=p.size==LumeMaterialParametersV1Size?0:p.cornerRadius;
  g.sceneAnchor=p.size>=LumeMaterialParametersV3Size?p.sceneAnchor:nullptr;
  g.bottomInset=p.size>=LumeMaterialParametersV4Size?static_cast<int>(p.bottomInset):0;
  g.innerRadius=p.size>=LumeMaterialParametersV5Size?static_cast<float>(p.innerRadius):0;
  if(!timed)g.opacity=p.size>=LumeMaterialParametersV6Size&&p.opacityEnabled?p.opacity:1.f;
  {std::lock_guard<std::mutex> lock(g.contentMutex);
   const bool v7=p.size==sizeof(p);
   g.lensOverride=v7&&p.lensOverride!=0;
   if(g.lensOverride&&!timed){g.lensRect[0]=p.lensX;g.lensRect[1]=p.lensY;g.lensRect[2]=p.lensWidth;g.lensRect[3]=p.lensHeight;}
   g.contentEnabled=v7&&p.contentEnabled!=0;
   if(!timed)g.contentOpacity=1;
   if(g.contentEnabled){
     if(!timed){g.contentRect[0]=p.contentX;g.contentRect[1]=p.contentY;g.contentRect[2]=p.contentWidth;g.contentRect[3]=p.contentHeight;}
     if(p.contentGeneration!=g.contentGeneration||p.contentPixelWidth!=g.contentPixelWidth||p.contentPixelHeight!=g.contentPixelHeight){
       g.contentPixels.assign(p.contentPixels,p.contentPixels+static_cast<size_t>(p.contentPixelWidth)*p.contentPixelHeight);
       g.contentPixelWidth=p.contentPixelWidth;g.contentPixelHeight=p.contentPixelHeight;g.contentGeneration=p.contentGeneration;
       ++g.contentRevision;
     }
   }
   if(v7&&p.timelineCount){
     if(p.timelineStart!=g.timelineStart||p.timelineCount!=g.timeline.size()){
       g.timeline.assign(p.timeline,p.timeline+p.timelineCount);
       for(auto& frame:g.timeline)frame.panelCount=(std::min)(frame.panelCount,8u);
       g.timelineStep=p.timelineStepMs;g.timelineStart=p.timelineStart;
       if(p.lensOverride==2)AnchorStage(g);
     }
   }else{g.timeline.clear();g.timelineStart=0;}
   g.anchored=v7&&p.lensOverride==2&&!g.timeline.empty();}
  {std::lock_guard<std::mutex> lock(g.interactionMutex);
   g.interactionInput.pressed=p.pressed!=0;g.interactionInput.reducedMotion=p.reducedMotion!=0;
   g.interactionInput.x=p.contactX;g.interactionInput.y=p.contactY;}
  if(!timed){std::lock_guard<std::mutex> lock(g.discsMutex);g.panels.clear();
   for(uint32_t i=0;i<p.panelCount;++i){const auto& r=p.panels[i];g.panels.push_back({r.x,r.y,r.width,r.height,r.radius});}}
  ++g.revision;g.wake->Notify();
}
// The DLL can stay loaded while the last material worker closes and another
// starts later. Release WinRT's cached activation factories before the last
// apartment shuts down, or a subsequent scene can call an unloaded vtable.
std::mutex g_apartmentMutex;
unsigned g_apartmentUsers = 0;
struct MaterialApartment {
  MaterialApartment() {
    std::lock_guard<std::mutex> lock(g_apartmentMutex);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    ++g_apartmentUsers;
  }
  ~MaterialApartment() {
    std::lock_guard<std::mutex> lock(g_apartmentMutex);
    if (--g_apartmentUsers == 0) winrt::clear_factory_cache();
    winrt::uninit_apartment();
  }
};
void Run(GlassInstance* raw) {
  try {
    MaterialApartment apartment;
    while(!raw->stop) {
      raw->readySources=0;raw->desktopIconSources=0;raw->desktopIconReady=0;
      raw->initialized=false;raw->frameReady=false;raw->lastError=S_OK;
      try {InitializeAndRun(raw);}
      catch(const winrt::hresult_error& e){raw->lastError=e.code();SignalInitialization(raw,false);}
      catch(...){raw->lastError=E_FAIL;SignalInitialization(raw,false);}
      raw->initialized=false;
      if(raw->surface){DestroyWindow(raw->surface);raw->surface=nullptr;}
      raw->scenePixels=0;raw->surfacePixels=0;raw->frameReady=false;
      if(raw->failed || FAILED(raw->lastError.load())) {
        raw->failed=true;
        NotifyStatus(raw);
        // Release the failed scene before retrying, and bound retry frequency
        // even if input/capture events keep waking the signal. Detach interrupts.
        const ULONGLONG retryAt=GetTickCount64()+2000;
        while(!raw->stop && GetTickCount64()<retryAt)
          WaitForSingleObject(raw->wake->Handle(),100);
      }
      NotifyStatus(raw);
      while(!raw->stop && IsWindow(raw->owner) && (!IsWindowVisible(raw->owner)||IsIconic(raw->owner)))
        WaitForSingleObject(raw->wake->Handle(),kHiddenResumeWaitMs);
      if(!IsWindow(raw->owner))raw->stop=true;
    }
  }catch(...){raw->lastError=E_FAIL;SignalInitialization(raw,false);}
  RemovePropW(raw->owner,lume::window_ids::GlassOwnerProp);
  SetEvent(raw->workerExited.get());
}
} // namespace

extern "C" __declspec(dllexport) LumeMaterialHandle __cdecl LumeMaterialAttach(const LumeMaterialParameters* p) {
  try {
    if(!Valid(p))return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if(GetPropW(p->owner,lume::window_ids::GlassOwnerProp) || !RegisterSurfaceClass())return 0;
    auto g=std::make_unique<GlassInstance>();if(!g->workerExited)return 0;
    g->owner=p->owner;Configure(*g,*p);RefreshLiveLens(g.get());
    const auto id=g_nextId++;
    // Allocate the map entry before starting a worker, so a failed allocation cannot destroy a joinable thread.
    auto inserted=g_glasses.emplace(id,std::move(g));auto* raw=inserted.first->second.get();
    SetPropW(raw->owner,lume::window_ids::GlassOwnerProp,reinterpret_cast<HANDLE>(1));
    try {raw->worker=std::thread(Run,raw);}catch(...){RemovePropW(raw->owner,lume::window_ids::GlassOwnerProp);g_glasses.erase(id);throw;}
    return id;
  }catch(...){return 0;}
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialUpdate(LumeMaterialHandle id,const LumeMaterialParameters* p) {
  try {if(!Valid(p))return 0;std::lock_guard<std::mutex> lock(g_mutex);auto i=g_glasses.find(static_cast<uint32_t>(id));
    if(i==g_glasses.end()||i->second->owner!=p->owner)return 0;Configure(*i->second,*p);return 1;
  }catch(...){return 0;}
}
extern "C" __declspec(dllexport) void __cdecl LumeMaterialDetach(LumeMaterialHandle id) {
  std::unique_ptr<GlassInstance> g;
  {std::lock_guard<std::mutex> lock(g_mutex);auto i=g_glasses.find(static_cast<uint32_t>(id));if(i==g_glasses.end())return;g=std::move(i->second);g_glasses.erase(i);}
  g->stop=true;g->wake->Notify();if(g->surface)PostMessageW(g->surface,WM_NULL,0,0);
  if(g->worker.joinable()) {
    // The worker owns a window, and its z-order/destruction calls can send
    // synchronous messages to the owner. A plain join on that UI thread can
    // deadlock. Service only sent messages while retiring this lease: queued
    // input, timers and material callbacks must not reenter closing controls.
    const HANDLE exited=g->workerExited.get();
    while(MsgWaitForMultipleObjectsEx(1,&exited,INFINITE,QS_SENDMESSAGE,0)==WAIT_OBJECT_0+1) {
      MSG message{};
      PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
    }
    g->worker.join();
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if(g_glasses.empty())UnregisterClassW(lume::window_ids::GlassSurfaceClass,MaterialModule());
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialInspect(LumeMaterialHandle id,LumeMaterialStatus* s) {
  if(!s||s->size!=sizeof(*s))return 0;
  std::lock_guard<std::mutex> lock(g_mutex);auto i=g_glasses.find(static_cast<uint32_t>(id));if(i==g_glasses.end())return 0;
  const auto& g=*i->second;s->initialized=g.initialized;s->frameReady=g.frameReady;s->failed=g.failed;s->frames=g.frames;
  s->lastError=g.lastError;s->sources=g.sources;s->readySources=g.readySources;s->scenePixels=g.scenePixels;s->surfacePixels=g.surfacePixels;return 1;
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialStatistics(LumeMaterialStatisticsData* s) {
  if(!s||s->size!=sizeof(*s))return 0;
  std::lock_guard<std::mutex> lock(g_mutex);s->users=static_cast<uint32_t>(g_glasses.size());
  s->devices=g_glassCaptureRegistry.DeviceCount();s->captures=g_glassCaptureRegistry.CaptureCount();return 1;
}
