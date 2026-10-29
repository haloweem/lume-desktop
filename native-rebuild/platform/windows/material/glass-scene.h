#pragma once

#include "desktop-window.h"
#include "../lume_window_ids.h"

// Window capture supplies pixels before glass composition, including the area
// beneath a stationary lens. Monitor capture of that area contains glass itself.
class GlassScene {
  using Capture = GlassCaptureRegistry::Capture;
  using Graphics = GlassCaptureRegistry::Graphics;
  struct Layer {
    std::shared_ptr<Capture> capture;
    std::shared_ptr<lume_glass::FrameNotifications::Subscription> subscription;
    ULONGLONG lastUsed = 0;
    ULONGLONG openedAt = 0;
    ULONGLONG revision = 0;
  };
  struct Source {
    HWND window;
    RECT bounds;
    bool needed;
    bool desktopHost;
    bool desktopIconView;
    bool nativeSurface;
    std::shared_ptr<GlassCaptureRegistry::SurfaceFeed> surface;
  };
  std::shared_ptr<Graphics> graphics;
  std::shared_ptr<lume_glass::WakeSignal> wake;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  ComPtr<ID3D11RenderTargetView> target;
  // Optional reduced-resolution copy of the composed scene, used as the
  // material's backdrop so a wide lens shows a softer field (see Render).
  ComPtr<ID3D11Texture2D> backdrop;
  ComPtr<ID3D11ShaderResourceView> backdropView;
  ComPtr<ID3D11RenderTargetView> backdropTarget;
  int backdropWidth = 0, backdropHeight = 0;
  int backdropScale = 1;
  ComPtr<ID3D11PixelShader> blit, clear;
  ComPtr<ID3D11BlendState> blend;
  std::unordered_map<HWND, std::unique_ptr<Layer>> layers;
  std::unordered_map<HWND, ULONGLONG> retryAfter;
  int left = 0, top = 0, width = 1, height = 1;
  std::vector<Source> previousSources;
  RECT previousLens{};
  bool pendingScene = false;
  bool presentable = false;
  bool capturesClosed = false;

  static bool Bounds(HWND hwnd, RECT* bounds) {
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, bounds, sizeof(*bounds))))
      return GetWindowRect(hwnd, bounds) != FALSE;
    return true;
  }

  static std::vector<RECT> Subtract(RECT source, const RECT& cut) {
    RECT overlap{};
    if (!IntersectRect(&overlap, &source, &cut)) return {source};
    std::vector<RECT> result;
    if (source.top < overlap.top) result.push_back({source.left, source.top, source.right, overlap.top});
    if (overlap.bottom < source.bottom) result.push_back({source.left, overlap.bottom, source.right, source.bottom});
    if (source.left < overlap.left) result.push_back({source.left, overlap.top, overlap.left, overlap.bottom});
    if (overlap.right < source.right) result.push_back({overlap.right, overlap.top, source.right, overlap.bottom});
    return result;
  }

  std::vector<Source> SourcesBelow(HWND owner, HWND surface, const RECT& lens) {
    std::vector<Source> sources;
    std::vector<HWND> lowerGlassOwners;
    const HWND desktopHost = FindDesktopHost();
    const HWND desktopIconView = FindDesktopIconView(desktopHost);
    // Opening a nearby stream allocates full-window WGC buffers, even if none
    // of its pixels are sampled. Only capture the lens and its optical margin.
    std::vector<RECT> uncovered = {lens};
    for (HWND hwnd = GetWindow(owner, GW_HWNDNEXT); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
      const bool isDesktopHost = hwnd == desktopHost;
      // Add Explorer's desktop layers explicitly below. Their child view is
      // not part of the top-level Z-order walk, and treating the parent as a
      // normal opaque window can hide the icon layer from the lens.
      if (isDesktopHost) continue;
      if (hwnd == surface || !IsWindow(hwnd) || IsIconic(hwnd) ||
          (!IsWindowVisible(hwnd) && !isDesktopHost)) continue;
      const bool glassOwner = GetPropW(hwnd, lume::window_ids::GlassOwnerProp) != nullptr;
      const HWND surfaceOwner = reinterpret_cast<HWND>(GetPropW(hwnd, lume::window_ids::GlassSurfaceOwnerProp));
      // A surface is eligible only after its owner was seen below this lens.
      // This excludes our own surface and stale surfaces during Z-order changes.
      if (surfaceOwner && std::find(lowerGlassOwners.begin(), lowerGlassOwners.end(), surfaceOwner) == lowerGlassOwners.end()) continue;
      if (glassOwner) lowerGlassOwners.push_back(hwnd);
      const LONG extended = GetWindowLongW(hwnd, GWL_EXSTYLE);
      if (!isDesktopHost && !surfaceOwner && !glassOwner &&
          (extended & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) == (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) continue;
      if (extended & WS_EX_LAYERED) {
        COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) continue;
      }
      DWORD cloaked = 0;
      if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
      wchar_t name[128]{};
      GetClassNameW(hwnd, name, ARRAYSIZE(name));
      if (wcscmp(name, L"LumeNativeGlassSurfaceV3") == 0 && !surfaceOwner) continue;
      const bool nativeSurface = surfaceOwner != nullptr;
      const auto surface = nativeSurface ? g_glassCaptureRegistry.Surface(hwnd) : nullptr;
      // Native surfaces are composited from their shared output, not captured
      // through WGC. Until published, use the transparent owner temporarily.
      if (nativeSurface && (!surface || !surface->view)) continue;
      RECT bounds{}, overlap{};
      if (nativeSurface) bounds = surface->bounds;
      else if (!Bounds(hwnd, &bounds) || IsRectEmpty(&bounds)) continue;
      if (IsRectEmpty(&bounds)) continue;
      RECT coverage = bounds;
      if (surfaceOwner) {
        if (!Bounds(surfaceOwner, &coverage)) continue;
        InflateRect(&coverage, 34, 34);
      }
      bool visible = false;
      for (const auto& region : uncovered) {
        if (IntersectRect(&overlap, &region, &coverage)) { visible = true; break; }
      }
      if (!visible) continue;
      sources.push_back({hwnd, bounds, IntersectRect(&overlap, &coverage, &lens) != FALSE,
          false, false, nativeSurface, surface});
      // Preserve a border for rounded corners and shadows over lower windows.
      if (!glassOwner && !surfaceOwner && !(extended & WS_EX_LAYERED)) {
        RECT interior = bounds;
        InflateRect(&interior, -16, -16);
        if (!IsRectEmpty(&interior)) {
          std::vector<RECT> remaining;
          for (const auto& region : uncovered) {
            auto pieces = Subtract(region, interior);
            remaining.insert(remaining.end(), pieces.begin(), pieces.end());
          }
          uncovered = std::move(remaining);
        }
      }
      if (uncovered.empty()) break;
    }
    // Graphics Capture accepts the top-level Explorer host reliably. A direct
    // capture of SHELLDLL_DefView is not guaranteed to produce frames because
    // it is a child window, so use it as an icon-presence anchor while the
    // parent supplies the already-composed wallpaper + icon pixels.
    auto appendDesktopLayer = [&](HWND window, bool isHost, bool containsIcons) {
      if (!window || std::any_of(sources.begin(), sources.end(), [window](const Source& source) {
            return source.window == window;
          })) return;
      RECT bounds{};
      if (!Bounds(window, &bounds) || IsRectEmpty(&bounds)) {
        const HMONITOR monitor = MonitorFromRect(&lens, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {sizeof(MONITORINFO)};
        if (!monitor || !GetMonitorInfoW(monitor, &info)) bounds = lens;
        else bounds = info.rcMonitor;
      }
      RECT overlap{};
      if (IntersectRect(&overlap, &bounds, &lens))
        sources.push_back({window, bounds, true, isHost, containsIcons, false, nullptr});
    };
    // A fully opaque app already covering the optical margin makes Explorer
    // irrelevant. Capturing it anyway keeps a static lens awake when wallpaper
    // animates (and needlessly allocates another full-desktop capture stream).
    if (!uncovered.empty()) appendDesktopLayer(desktopHost, true, desktopIconView != nullptr);
    return sources;
  }

  Layer* OpenLayer(HWND hwnd) {
    auto found = layers.find(hwnd);
    if (found != layers.end()) return found->second.get();
    if (retryAfter[hwnd] > GetTickCount64() || !graphics) return nullptr;
    auto capture = g_glassCaptureRegistry.AcquireCapture(hwnd, graphics);
    if (!capture) {
      retryAfter[hwnd] = GetTickCount64() + 1000;
      return nullptr;
    }
    auto layer = std::make_unique<Layer>();
    layer->openedAt = GetTickCount64();
    layer->capture = std::move(capture);
    layer->subscription = layer->capture->notifications->Subscribe(wake);
    auto* result = layer.get();
    layers.emplace(hwnd, std::move(layer));
    retryAfter.erase(hwnd);
    return result;
  }

  bool UpdateLayer(Layer* layer) {
    if (!layer || !layer->capture || !graphics) return false;
    g_glassCaptureRegistry.Update(layer->capture, graphics);
    // Another scene may have dequeued the new frame. Each consumer must still
    // invalidate its own scene texture when it observes that revision.
    if (layer->revision != layer->capture->revision) ++captureFrames;
    layer->revision = layer->capture->revision;
    return !layer->capture->failed;
  }

public:
  // Whether captures were closed since the last call; the caller then flushes
  // and trims the device (see the Prune call in Render).
  bool TakeClosedCaptures() { const bool closed = capturesClosed; capturesClosed = false; return closed; }
  unsigned sourceCount = 0, readyCount = 0, captureFrames = 0, deferredScenes = 0;
  unsigned desktopIconSources = 0, desktopIconReady = 0;
  ~GlassScene() {
    layers.clear();
    if (graphics) {
      g_glassCaptureRegistry.ReleaseGraphics();
      graphics.reset();
    }
  }

  bool Initialize(const std::shared_ptr<Graphics>& sharedGraphics, const std::shared_ptr<lume_glass::WakeSignal>& wakeSignal,
      int x, int y, int w, int h) {
    left = x; top = y; width = w; height = h;
    graphics = sharedGraphics;
    wake = wakeSignal;
    auto* device = graphics ? graphics->device.Get() : nullptr;
    auto* context = graphics ? graphics->context.Get() : nullptr;
    try {
      if (!device || !context || !winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) return false;
      namespace shaders = lume_material_bytecode;
      winrt::check_hresult(device->CreatePixelShader(shaders::sceneBlit, sizeof(shaders::sceneBlit), nullptr, blit.GetAddressOf()));
      winrt::check_hresult(device->CreatePixelShader(shaders::sceneClear, sizeof(shaders::sceneClear), nullptr, clear.GetAddressOf()));
      D3D11_BLEND_DESC blendDesc{};
      auto& b = blendDesc.RenderTarget[0];
      b.BlendEnable = TRUE; b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
      b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
      b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      winrt::check_hresult(device->CreateBlendState(&blendDesc, blend.GetAddressOf()));
      return true;
    } catch (const winrt::hresult_error& error) { LogNative("SceneInit", error.code()); return false; }
  }

  bool Render(HWND owner, HWND surface, const RECT& lens, ID3D11Device* device, ID3D11DeviceContext* context) {
    const auto visible = SourcesBelow(owner, surface, lens);
    const unsigned beforeFrames = captureFrames;
    bool changed = pendingScene || !texture || !EqualRect(&lens, &previousLens) || visible.size() != previousSources.size();
    if (!changed) {
      for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i].window != previousSources[i].window ||
            !EqualRect(&visible[i].bounds, &previousSources[i].bounds) ||
            visible[i].surface != previousSources[i].surface ||
            (visible[i].surface && previousSources[i].surface &&
             visible[i].surface->revision != previousSources[i].surface->revision)) { changed = true; break; }
      }
    }
    const unsigned beforeReady = readyCount;
    bool waitingForFirstFrame = false;
    sourceCount = readyCount = desktopIconSources = desktopIconReady = 0;
    for (const auto& source : visible) {
      if (source.needed) ++sourceCount;
      if (source.desktopIconView) ++desktopIconSources;
      if (source.nativeSurface) {
        if (source.needed && source.surface && source.surface->view) {
          ++readyCount;
        }
        continue;
      }
      auto* layer = OpenLayer(source.window);
      if (!layer) continue;
      layer->lastUsed = GetTickCount64();
      if (!UpdateLayer(layer)) { layers.erase(source.window); continue; }
      if (source.needed && layer->capture && layer->capture->view) {
        ++readyCount;
        if (source.desktopIconView) ++desktopIconReady;
      }
      else if (source.needed && GetTickCount64() - layer->openedAt < 250) waitingForFirstFrame = true;
    }
    for (auto it = layers.begin(); it != layers.end();) {
      if (!IsWindow(it->first) || GetTickCount64() - it->second->lastUsed > 500) it = layers.erase(it);
      else ++it;
    }
    for (auto it = retryAfter.begin(); it != retryAfter.end();) {
      if (!IsWindow(it->first)) it = retryAfter.erase(it);
      else ++it;
    }
    // Windows passing under the glass open full-window captures. Closing them
    // releases their textures only on the next flush; a static lens may not
    // present again for minutes, so the memory would otherwise accumulate.
    if (g_glassCaptureRegistry.Prune()) capturesClosed = true;
    // A new WGC session has no pixels until its first asynchronous frame.
    // Keep the committed scene intact during that handoff; clearing it or
    // hiding the surface makes the entire material flash at contact. A broken
    // source must not indefinitely freeze otherwise healthy background feeds.
    if (waitingForFirstFrame) {
      pendingScene = true;
      ++deferredScenes;
      return false;
    }
    pendingScene = false;
    if (!changed && captureFrames == beforeFrames && readyCount == beforeReady) return false;
    previousSources = visible;
    previousLens = lens;
    left = lens.left; top = lens.top;
    const int nextWidth = ((lens.right - lens.left + 63) / 64) * 64;
    const int nextHeight = ((lens.bottom - lens.top + 63) / 64) * 64;
    if (!texture || nextWidth != width || nextHeight != height) {
      view.Reset(); target.Reset(); texture.Reset();
      backdrop.Reset(); backdropView.Reset(); backdropTarget.Reset();
      backdropWidth = backdropHeight = 0;
      width = nextWidth; height = nextHeight;
      D3D11_TEXTURE2D_DESC desc{};
      // Mipmapped: a wide frost (the Dock's) reads a downsampled level, so fine
      // wallpaper detail averages out instead of aliasing into grain. All other
      // consumers sample level 0 explicitly.
      desc.Width = width; desc.Height = height; desc.ArraySize = 1; desc.MipLevels = 0;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
      winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()));
      winrt::check_hresult(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
      winrt::check_hresult(device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf()));
    }
    // The scene needs only the sampled region, not a virtual-desktop-sized
    // allocation for every widget. Source viewports retain screen coordinates.
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    RECT scissor = {(std::max)(0L, lens.left - left), (std::max)(0L, lens.top - top),
        (std::min)(static_cast<LONG>(width), lens.right - left), (std::min)(static_cast<LONG>(height), lens.bottom - top)};
    context->RSSetScissorRects(1, &scissor);
    D3D11_VIEWPORT full = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context->RSSetViewports(1, &full);
    context->PSSetShader(clear.Get(), nullptr, 0);
    context->Draw(3, 0);
    context->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
    context->PSSetShader(blit.Get(), nullptr, 0);
    for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
      auto found = layers.find(it->window);
      if (!it->needed) continue;
      if (it->nativeSurface) {
        if (!it->surface || !it->surface->view) continue;
        D3D11_VIEWPORT viewport = {
          static_cast<float>(it->bounds.left - left), static_cast<float>(it->bounds.top - top),
          static_cast<float>(it->bounds.right - it->bounds.left),
          static_cast<float>(it->bounds.bottom - it->bounds.top), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->PSSetShaderResources(0, 1, it->surface->view.GetAddressOf());
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        continue;
      }
      if (found == layers.end() || !found->second->capture || !found->second->capture->view) continue;
      const auto& rect = it->bounds;
      D3D11_VIEWPORT viewport = {static_cast<float>(rect.left - left), static_cast<float>(rect.top - top),
          static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top), 0, 1};
      context->RSSetViewports(1, &viewport);
      context->PSSetShaderResources(0, 1, found->second->capture->view.GetAddressOf());
      context->Draw(3, 0);
      ID3D11ShaderResourceView* empty = nullptr;
      context->PSSetShaderResources(0, 1, &empty);
    }
    // A launchpad-sized lens covers several desktop icons at once. The material
    // kernel is only 9 taps wide, so sampling the full-resolution scene leaves a
    // faint icon/window grid in the frost. Downsampling the backdrop first
    // widens the effective blur by the same factor (and costs less memory).
    if (backdropScale > 1 && view && (backdropWidth != width / backdropScale ||
        backdropHeight != height / backdropScale || !backdropView)) {
      backdrop.Reset(); backdropView.Reset(); backdropTarget.Reset();
      backdropWidth = (std::max)(1, width / backdropScale);
      backdropHeight = (std::max)(1, height / backdropScale);
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = backdropWidth; desc.Height = backdropHeight;
      desc.ArraySize = desc.MipLevels = 1;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, backdrop.GetAddressOf())) ||
          FAILED(device->CreateShaderResourceView(backdrop.Get(), nullptr, backdropView.GetAddressOf())) ||
          FAILED(device->CreateRenderTargetView(backdrop.Get(), nullptr, backdropTarget.GetAddressOf()))) {
        backdrop.Reset(); backdropView.Reset(); backdropTarget.Reset();
        backdropWidth = backdropHeight = 0;
      }
    }
    if (backdropView) {
      context->OMSetRenderTargets(1, backdropTarget.GetAddressOf(), nullptr);
      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
      RECT all = {0, 0, backdropWidth, backdropHeight};
      context->RSSetScissorRects(1, &all);
      D3D11_VIEWPORT down = {0, 0, static_cast<float>(backdropWidth),
          static_cast<float>(backdropHeight), 0, 1};
      context->RSSetViewports(1, &down);
      context->PSSetShader(blit.Get(), nullptr, 0);
      context->PSSetShaderResources(0, 1, view.GetAddressOf());
      context->Draw(3, 0);
      ID3D11ShaderResourceView* empty = nullptr;
      context->PSSetShaderResources(0, 1, &empty);
    }
    context->OMSetRenderTargets(0, nullptr, nullptr);
    if (view) context->GenerateMips(view.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    presentable = readyCount > 0;
    return true;
  }

  static bool HasDesktopView(HWND hwnd) {
    return hwnd && FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
  }

  static HWND FindDesktopIconView(HWND host) {
    if (!host) return nullptr;
    HWND view = FindWindowExW(host, nullptr, L"SHELLDLL_DefView", nullptr);
    if (view) return view;
    HWND result = nullptr;
    EnumChildWindows(host, [](HWND hwnd, LPARAM data) -> BOOL {
      wchar_t name[64]{};
      GetClassNameW(hwnd, name, ARRAYSIZE(name));
      if (wcscmp(name, L"SHELLDLL_DefView") == 0) {
        *reinterpret_cast<HWND*>(data) = hwnd;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  // Explorer can host the desktop icon list in Progman or in a WorkerW
  // sibling. The host is sometimes hidden from the normal visible-window
  // walk, so keep an explicit lookup instead of relying on its Z-order.
  static HWND FindDesktopHost() {
    // GetShellWindow is more reliable than FindWindowW while Explorer is
    // rebuilding the desktop (and also works with alternate shell layouts).
    const HWND shell = GetShellWindow();
    const HWND progman = shell && lume::IsDesktopHost(shell) ? shell : FindWindowW(L"Progman", nullptr);
    if (progman && lume::IsDesktopHost(progman) && HasDesktopView(progman)) return progman;
    HWND result = nullptr;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
      auto* found = reinterpret_cast<HWND*>(data);
      if (lume::IsDesktopHost(hwnd) && HasDesktopView(hwnd)) {
        *found = hwnd;
        return FALSE;
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    if (result) return result;
    return progman && lume::IsDesktopHost(progman) ? progman : nullptr;
  }

  RECT Bounds() const { return {left, top, left + width, top + height}; }
  RECT PresentationBounds(const RECT& lens) const {
    if (!pendingScene || !texture || IsRectEmpty(&previousLens)) return Bounds();
    // The retained scene follows the lens until capture catches up. Its old
    // screen-space clip would otherwise cut holes during a fast move/resize.
    const double scaleX = static_cast<double>(lens.right - lens.left) / (previousLens.right - previousLens.left);
    const double scaleY = static_cast<double>(lens.bottom - lens.top) / (previousLens.bottom - previousLens.top);
    return {lens.left, lens.top, lens.left + static_cast<LONG>(std::ceil(width * scaleX)),
        lens.top + static_cast<LONG>(std::ceil(height * scaleY))};
  }
  bool Presentable() const { return presentable; }
  unsigned Pixels() const { return texture ? width * height : 0; }
  ID3D11ShaderResourceView* View() const { return backdropView ? backdropView.Get() : view.Get(); }
  // 1 keeps the full-resolution scene; larger values blur the backdrop by the
  // same factor without growing the material kernel.
  void SetBackdropScale(int scale) {
    const int next = std::clamp(scale, 1, 8);
    if (next == backdropScale) return;
    backdropScale = next;
    if (next <= 1) {
      backdrop.Reset(); backdropView.Reset(); backdropTarget.Reset();
      backdropWidth = backdropHeight = 0;
    }
  }
  uint32_t Sample(ID3D11Device* device, ID3D11DeviceContext* context, int x, int y) {
    x -= left; y -= top;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> sample;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, sample.GetAddressOf()))) return 0;
    D3D11_BOX region = {static_cast<UINT>(x), static_cast<UINT>(y), 0,
        static_cast<UINT>(x + 1), static_cast<UINT>(y + 1), 1};
    context->CopySubresourceRegion(sample.Get(), 0, 0, 0, 0, texture.Get(), 0, &region);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(sample.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 0;
    uint32_t color = *static_cast<uint32_t*>(mapped.pData);
    context->Unmap(sample.Get(), 0);
    return color;
  }
};
