#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <windows.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <vector>

// Process-wide graphics and Windows Graphics Capture ownership.  Each glass
// scene keeps its own composition target and scene texture, while capture
// sessions and their shader-readable frames are shared by HWND.
class GlassCaptureRegistry {
public:
  using ShaderViewPtr = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
  using Item = winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
  using Pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
  using Session = winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
  using Frame = winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
  using WinDevice = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

  struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    WinDevice winDevice{nullptr};
    std::mutex contextMutex;
  };

  struct Capture {
    HWND hwnd = nullptr;
    Item item{nullptr};
    Pool pool{nullptr};
    Session session{nullptr};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    Frame retainedFrame{nullptr};
    winrt::Windows::Graphics::SizeInt32 size{};
    ULONGLONG lastUsed = 0;
    ULONGLONG retryAfter = 0;
    uint64_t revision = 0;
    bool failed = false;
    std::shared_ptr<lume_glass::FrameNotifications> notifications = std::make_shared<lume_glass::FrameNotifications>();
    winrt::event_token frameArrived{};

    ~Capture() {
      // Drop our view of the retained frame before closing it and its pool;
      // a pool closed while a view still references one of its buffers
      // orphans that buffer in the capture service (one frame per stream).
      view.Reset();
      texture.Reset();
      try {
        if (pool && frameArrived.value) pool.FrameArrived(frameArrived);
        if (retainedFrame) retainedFrame.Close();
        if (session) session.Close();
        if (pool) pool.Close();
      } catch (...) {}
    }
  };

  // Publish native output directly to lower-to-upper scene composition. This
  // avoids capturing glass as its own input without hiding it in screenshots.
  struct SurfaceFeed {
    HWND surface = nullptr;
    HWND owner = nullptr;
    RECT bounds{};
    ShaderViewPtr view;
    ULONGLONG revision = 0;
  };

private:
  mutable std::mutex mutex_;
  std::shared_ptr<Graphics> graphics_;
  unsigned users_ = 0;
  std::unordered_map<HWND, std::shared_ptr<Capture>> captures_;
  std::unordered_map<HWND, std::shared_ptr<SurfaceFeed>> surfaces_;

public:
  std::shared_ptr<Graphics> AcquireGraphics(IDXGIAdapter* adapter) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Retire a removed device even while other workers are unwinding their own
    // scenes. Those scenes hold shared_ptr leases; the next generation must not
    // inherit their failed HWND capture textures.
    if (graphics_ && FAILED(graphics_->device->GetDeviceRemovedReason())) {
      captures_.clear();
      surfaces_.clear();
      graphics_.reset();
    }
    if (!graphics_) {
      auto graphics = std::make_shared<Graphics>();
      const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
      D3D_FEATURE_LEVEL selected = {};
      winrt::check_hresult(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
          graphics->device.GetAddressOf(), &selected, graphics->context.GetAddressOf()));
      ComPtr<IDXGIDevice> dxgi;
      winrt::check_hresult(graphics->device.As(&dxgi));
      winrt::com_ptr<IInspectable> inspectable;
      winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
      graphics->winDevice = inspectable.as<WinDevice>();
      graphics_ = std::move(graphics);
    }
    ++users_;
    return graphics_;
  }

  void ReleaseGraphics() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_ > 0) --users_;
    if (users_ == 0) {
      captures_.clear();
      surfaces_.clear();
      graphics_.reset();
    }
  }

  void RegisterSurface(HWND surface, HWND owner) {
    if (!surface) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& feed = surfaces_[surface];
    if (!feed) feed = std::make_shared<SurfaceFeed>();
    feed->surface = surface;
    feed->owner = owner;
  }

  void PublishSurface(HWND surface, const RECT& bounds, ID3D11ShaderResourceView* view) {
    if (!surface || !view) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& feed = surfaces_[surface];
    if (!feed) feed = std::make_shared<SurfaceFeed>();
    feed->surface = surface;
    feed->bounds = bounds;
    feed->view = view;
    ++feed->revision;
  }

  void UnregisterSurface(HWND surface) {
    if (!surface) return;
    std::lock_guard<std::mutex> lock(mutex_);
    surfaces_.erase(surface);
  }

  std::shared_ptr<SurfaceFeed> Surface(HWND surface) const {
    if (!surface) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = surfaces_.find(surface);
    return found == surfaces_.end() ? nullptr : found->second;
  }

  std::shared_ptr<Capture> AcquireCapture(HWND hwnd, const std::shared_ptr<Graphics>& graphics) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = captures_.find(hwnd);
    if (found != captures_.end()) {
      if (!found->second->failed || found->second->retryAfter > GetTickCount64()) return found->second;
      if (found->second.use_count() > 1) return found->second;
      captures_.erase(found);
    }
    auto capture = std::make_shared<Capture>();
    capture->hwnd = hwnd;
    try {
      auto interop = winrt::get_activation_factory<Item, IGraphicsCaptureItemInterop>();
      winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<Item>(), winrt::put_abi(capture->item)));
      capture->size = capture->item.Size();
      if (capture->size.Width <= 0 || capture->size.Height <= 0) return nullptr;
      capture->pool = Pool::CreateFreeThreaded(graphics->winDevice,
          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          2, capture->size);
      capture->session = capture->pool.CreateCaptureSession(capture->item);
      capture->frameArrived = capture->pool.FrameArrived([notifications = capture->notifications](const Pool&, const auto&) {
        notifications->Notify();
      });
      capture->session.IsCursorCaptureEnabled(false);
      try { capture->session.IsBorderRequired(false); } catch (...) {}
      capture->session.StartCapture();
      captures_.emplace(hwnd, capture);
      return capture;
    } catch (const winrt::hresult_error& error) {
      capture->failed = true;
      capture->retryAfter = GetTickCount64() + 1000;
      LogNative("WindowCapture", error.code());
      return nullptr;
    }
  }

  bool Update(const std::shared_ptr<Capture>& capture, const std::shared_ptr<Graphics>& graphics) {
    if (!capture || capture->failed || !capture->pool) return false;
    capture->lastUsed = GetTickCount64();
    try {
      auto frame = capture->pool.TryGetNextFrame();
      if (!frame) return false;
      for (int i = 0; i < 8; ++i) {
        auto next = capture->pool.TryGetNextFrame();
        if (!next) break;
        frame.Close();
        frame = std::move(next);
      }
      const auto size = frame.ContentSize();
      if (size.Width <= 0 || size.Height <= 0) { frame.Close(); return false; }
      auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
      ComPtr<ID3D11Texture2D> captured;
      winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(captured.GetAddressOf())));
      D3D11_TEXTURE2D_DESC desc{}, previous{};
      captured->GetDesc(&desc);
      ComPtr<ID3D11ShaderResourceView> capturedView;
      const bool resized = size.Width != capture->size.Width || size.Height != capture->size.Height;
      if (!resized && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
          SUCCEEDED(graphics->device->CreateShaderResourceView(captured.Get(), nullptr, capturedView.GetAddressOf()))) {
        capture->view = std::move(capturedView);
        capture->texture.Reset();
        if (capture->retainedFrame) capture->retainedFrame.Close();
        capture->retainedFrame = std::move(frame);
        ++capture->revision;
        return true;
      }
      if (capture->retainedFrame) {
        capture->view.Reset();  // before the frame it views, as in ~Capture
        capture->retainedFrame.Close();
        capture->retainedFrame = nullptr;
      }
      if (capture->texture) capture->texture->GetDesc(&previous);
      if (!capture->texture || desc.Width != previous.Width || desc.Height != previous.Height) {
        capture->view.Reset();
        capture->texture.Reset();
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        winrt::check_hresult(graphics->device->CreateTexture2D(&desc, nullptr, capture->texture.GetAddressOf()));
        winrt::check_hresult(graphics->device->CreateShaderResourceView(capture->texture.Get(), nullptr, capture->view.GetAddressOf()));
      }
      graphics->context->CopyResource(capture->texture.Get(), captured.Get());
      frame.Close();
      if (resized) {
        capture->size = size;
        capture->pool.Recreate(graphics->winDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2, size);
      }
      ++capture->revision;
      return true;
    } catch (const winrt::hresult_error& error) {
      capture->failed = true;
      LogNative("WindowCaptureUpdate", error.code());
      return false;
    } catch (...) {
      capture->failed = true;
      return false;
    }
  }

  // Returns whether a capture was closed; the caller must then flush the
  // device, because D3D only destroys released textures on a flush.
  //
  // Windows Graphics Capture keeps about one frame of GPU memory for every
  // session that is ever started, even after it is closed (measured on
  // Windows 11 26300: ~5.6 MB per session for a 1600x900 window, unbounded).
  // Reopening a stream for the same window after every popup or drag would
  // therefore leak on each use, so idle streams are kept for reuse: up to
  // kIdleKeepMs and kIdleKeepMax streams, oldest closed first. An idle stream
  // costs its two pool buffers until closed; that cost is bounded.
  bool Prune() {
    constexpr ULONGLONG kIdleKeepMs = 60000;
    constexpr size_t kIdleKeepMax = 4;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = GetTickCount64();
    bool closed = false;
    std::vector<std::pair<ULONGLONG, HWND>> idle;
    for (auto it = captures_.begin(); it != captures_.end();) {
      const auto& capture = it->second;
      const bool unused = capture.use_count() == 1 && now - capture->lastUsed > 500;
      if (capture.use_count() == 1 &&
          (!IsWindow(it->first) || (unused && (capture->failed || now - capture->lastUsed > kIdleKeepMs)))) {
        it = captures_.erase(it);
        closed = true;
        continue;
      }
      if (unused) idle.emplace_back(capture->lastUsed, it->first);
      ++it;
    }
    if (idle.size() > kIdleKeepMax) {
      std::sort(idle.begin(), idle.end());
      for (size_t i = 0; i < idle.size() - kIdleKeepMax; ++i) captures_.erase(idle[i].second);
      closed = true;
    }
    return closed;
  }

  unsigned CaptureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<unsigned>(captures_.size());
  }

  unsigned DeviceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return graphics_ ? 1u : 0u;
  }

  std::mutex& Mutex() { return mutex_; }
};

inline GlassCaptureRegistry g_glassCaptureRegistry;
