#pragma once
#include "capture_access.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <windows.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <vector>

// Process-wide graphics and Windows Graphics Capture ownership.  Each glass
// scene keeps its own composition target and scene texture, while capture
// sessions and their shader-readable frames are shared by HWND.
bool InitializeCaptureCleanupApartment() noexcept;
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
    char targetClass[96]{};
    // The cleanup queue may outlive every glass using this graphics device.
    std::shared_ptr<Graphics> graphics;
    Item item{nullptr};
    Pool pool{nullptr};
    Session session{nullptr};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    Frame retainedFrame{nullptr};
    winrt::Windows::Graphics::SizeInt32 size{};
    std::atomic<ULONGLONG> lastUsed{0};
    std::atomic<ULONGLONG> retryAfter{0};
    uint64_t revision = 0;
    std::atomic<bool> failed{false};
    // WGC is serialized per HWND. Other consumers keep its last published
    // texture instead of waiting for an OS call on another glass worker.
    std::mutex operationMutex;
    std::shared_ptr<lume_glass::FrameNotifications> notifications = std::make_shared<lume_glass::FrameNotifications>();
    winrt::event_token frameArrived{};

    HRESULT Close() noexcept {
      // Drop our view of the retained frame before closing it and its pool;
      // a pool closed while a view still references one of its buffers
      // orphans that buffer in the capture service (one frame per stream).
      view.Reset();
      texture.Reset();
      HRESULT result=S_OK;
      try { if (pool && frameArrived.value) pool.FrameArrived(frameArrived); } catch (...) {result=winrt::to_hresult();}
      try { if (retainedFrame) retainedFrame.Close(); } catch (...) {result=winrt::to_hresult();}
      try { if (session) session.Close(); } catch (...) {result=winrt::to_hresult();}
      try { if (pool) pool.Close(); } catch (...) {result=winrt::to_hresult();}
      return result;
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
  static bool PinModule() noexcept {
    HMODULE module=nullptr;
    // Deliberately pin the material DLL, never the host EXE. A blocked cleanup
    // can otherwise resume into unloaded code after the final glass detaches.
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PinModule),&module))return false;
    const auto* image=reinterpret_cast<const unsigned char*>(module);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||nt->Signature!=IMAGE_NT_SIGNATURE||
       !(nt->FileHeader.Characteristics&IMAGE_FILE_DLL))return false;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&PinModule),&module)!=FALSE;
  }
  static lume_glass::CaptureRetirementQueue& Retirement() {
    // Neither this queue nor the registry is destroyed from DLL_PROCESS_DETACH:
    // Windows may already have terminated a worker while it held a lock.
    static auto* queue=new lume_glass::CaptureRetirementQueue(&PinModule,&InitializeCaptureCleanupApartment);
    return *queue;
  }
  static void DestroyCapture(void* value) noexcept {
    auto* capture=static_cast<Capture*>(value);
    char detail[256]{};
    std::snprintf(detail,sizeof(detail),"target=%p class=%s",static_cast<void*>(capture->hwnd),capture->targetClass);
    lume::platform::windows::material_diagnostics::Write("CaptureCloseBegin",S_OK,detail);
    const auto started=GetTickCount64();
    auto graphics=capture->graphics;
    const auto result=capture->Close();
    delete capture; // Include COM member Release and the graphics lease in this interval.
    if(graphics){
      // Prune now returns before Close, so its former render-side Flush/Trim
      // cannot release these textures yet. Do it after their real COM release,
      // without holding this lock across any WGC lifecycle call.
      {
        std::lock_guard<std::mutex> lock(graphics->contextMutex);
        graphics->context->Flush();
        ComPtr<IDXGIDevice3> trim;
        if(SUCCEEDED(graphics->device.As(&trim)))trim->Trim();
      }
      graphics.reset();
    }
    char finished[320]{};
    std::snprintf(finished,sizeof(finished),"%s elapsedMs=%llu",detail,
        static_cast<unsigned long long>(GetTickCount64()-started));
    lume::platform::windows::material_diagnostics::Write("CaptureCloseEnd",result,finished);
  }
  mutable std::mutex mutex_;
  std::shared_ptr<Graphics> graphics_;
  unsigned users_ = 0;
  std::unordered_map<HWND, std::shared_ptr<Capture>> captures_;
  std::unordered_map<HWND, std::shared_ptr<SurfaceFeed>> surfaces_;

public:
  static bool PinLifetime() noexcept {return PinModule();}
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
    // Cached sessions also need permission: Windows can revoke access while
    // their frames are retained for reuse by another material scene.
    lume_glass::RequireMaterialCaptureAccess();
    std::unique_lock<std::mutex> lock(mutex_);
    auto found = captures_.find(hwnd);
    if (found != captures_.end()) {
      if (!found->second->failed || found->second->retryAfter > GetTickCount64()) return found->second;
      if (found->second.use_count() > 1) return found->second;
      captures_.erase(found);
    }
    auto& retirement=Retirement();
    const auto slot=retirement.Reserve(graphics.get());
    if(slot==lume_glass::CaptureRetirementQueue::Invalid){
      const auto stats=retirement.Inspect();char detail[192]{};
      std::snprintf(detail,sizeof(detail),"total=%u active=%u pending=%u cleaning=%u limit=%u",
          stats.total,stats.active,stats.pending,stats.cleaning,lume_glass::CaptureRetirementQueue::Capacity);
      lume::platform::windows::material_diagnostics::Write(stats.ready?"CaptureRetirementLimit":"CaptureCleanupUnavailable",
          HRESULT_FROM_WIN32(ERROR_BUSY),detail);
      return nullptr;
    }
    Capture* allocated=nullptr;
    try {allocated=new Capture;}catch(...){retirement.Abandon(slot);throw;}
    std::shared_ptr<Capture> capture(allocated,[slot](Capture* value){Retirement().Retire(slot,value,&DestroyCapture);});
    std::unique_lock<std::mutex> operation(capture->operationMutex);
    capture->hwnd = hwnd;
    capture->graphics=graphics;
    wchar_t targetClass[96]{};GetClassNameW(hwnd,targetClass,96);
    for(unsigned i=0;i<95&&targetClass[i];++i)capture->targetClass[i]=
        targetClass[i]>=32&&targetClass[i]<127?static_cast<char>(targetClass[i]):'?';
    captures_.emplace(hwnd,capture);
    // Publish the initializing lease before entering WGC, so unrelated HWNDs
    // and the UI statistics path never wait behind capture startup.
    lock.unlock();
    try {
      auto interop = winrt::get_activation_factory<Item, IGraphicsCaptureItemInterop>();
      winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<Item>(), winrt::put_abi(capture->item)));
      capture->size = capture->item.Size();
      if (capture->size.Width <= 0 || capture->size.Height <= 0) {
        capture->retryAfter=GetTickCount64()+1000;capture->failed=true;return capture;
      }
      capture->pool = Pool::CreateFreeThreaded(graphics->winDevice,
          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          2, capture->size);
      capture->session = capture->pool.CreateCaptureSession(capture->item);
      capture->frameArrived = capture->pool.FrameArrived([notifications = capture->notifications](const Pool&, const auto&) {
        notifications->Notify();
      });
      capture->session.IsCursorCaptureEnabled(false);
      lume_glass::StartMaterialCapture(capture->session,
          [] { return lume_glass::MaterialCaptureAccessStatus(true); },
          [] { return static_cast<HRESULT>(winrt::to_hresult()); });
      return capture;
    } catch (const lume_glass::CaptureCompatibilityFailure& error) {
      capture->retryAfter=GetTickCount64()+1000;capture->failed=true;
      LogNative("CaptureBorderConfiguration", error.error);
      throw;
    } catch (const winrt::hresult_error& error) {
      capture->retryAfter = GetTickCount64() + 1000;
      capture->failed = true;
      LogNative("WindowCapture", error.code());
      return capture;
    } catch (...) {
      capture->retryAfter=GetTickCount64()+1000;capture->failed=true;
      return capture;
    }
  }

  bool Update(const std::shared_ptr<Capture>& capture, const std::shared_ptr<Graphics>& graphics) {
    if (!capture || capture->failed) return false;
    std::unique_lock operation(capture->operationMutex,std::try_to_lock);
    if (!operation.owns_lock() || capture->failed || !capture->pool) return false;
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
        Frame previousFrame{nullptr};
        {
          std::lock_guard contextLock(graphics->contextMutex);
          capture->view = std::move(capturedView);
          capture->texture.Reset();
          previousFrame=std::move(capture->retainedFrame);
          ++capture->revision;
        }
        capture->retainedFrame = std::move(frame);
        if(previousFrame)previousFrame.Close();
        return true;
      }
      Frame previousFrame{nullptr};
      {
      std::lock_guard contextLock(graphics->contextMutex);
      if (capture->retainedFrame) {
        capture->view.Reset();
        previousFrame=std::move(capture->retainedFrame);
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
      ++capture->revision;
      }
      if(previousFrame)previousFrame.Close();
      frame.Close();
      if (resized) {
        capture->size = size;
        capture->pool.Recreate(graphics->winDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2, size);
      }
      return true;
    } catch (const winrt::hresult_error& error) {
      capture->retryAfter=GetTickCount64()+1000;
      capture->failed = true;
      LogNative("WindowCaptureUpdate", error.code());
      return false;
    } catch (...) {
      capture->retryAfter=GetTickCount64()+1000;
      capture->failed = true;
      return false;
    }
  }

  // Returns whether registry references were retired. Final OS Close and the
  // corresponding device Flush/Trim happen later on the cleanup worker.
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
      if(capture->failed&&capture->retryAfter>now&&IsWindow(it->first)){++it;continue;}
      const bool unused = capture.use_count() == 1 && now - capture->lastUsed > 500;
      if (capture.use_count() == 1 &&
          (!IsWindow(it->first) || (unused && (capture->failed || now - capture->lastUsed > kIdleKeepMs)))) {
        it = captures_.erase(it);
        closed = true;
        continue;
      }
      if (unused) idle.emplace_back(capture->lastUsed.load(), it->first);
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
    return Retirement().Inspect().total;
  }

  unsigned DeviceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Retirement().DeviceCount(graphics_.get());
  }

  std::mutex& Mutex() { return mutex_; }
};

inline GlassCaptureRegistry& g_glassCaptureRegistry=*new GlassCaptureRegistry;
