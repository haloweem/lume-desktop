#include "platform/windows/spectrum/spectrum_capture.h"
#include "core/spectrum/spectrum.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
namespace lume::platform::windows {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto Interval=std::chrono::milliseconds(16);  // about 60 frames a second
// Without packets for this long the output is silent: let the bars fall.
constexpr auto SilenceAfter=std::chrono::milliseconds(150);
// After this much silence stop waking the UI until sound returns.
constexpr auto IdleAfter=std::chrono::seconds(1);
// Notifications normally route immediately; a cheap ID check also covers a
// failed notification registration or a missed driver notification.
constexpr auto RouteCheckInterval=std::chrono::seconds(1);

struct RouteSignal { std::atomic<std::uint64_t> revision{0}; };
class EndpointNotifications final:public IMMNotificationClient {
public:
    explicit EndpointNotifications(std::shared_ptr<RouteSignal> signal):signal_(std::move(signal)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {
        if(!value)return E_POINTER;
        *value=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IMMNotificationClient))return E_NOINTERFACE;
        *value=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR) override {
        if(flow==eRender&&role==eConsole)signal_->revision.fetch_add(1,std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {Changed();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {Changed();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override {Changed();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override {return S_OK;}
private:
    // No device calls, UI ownership, locks or teardown on an audio callback.
    void Changed(){signal_->revision.fetch_add(1,std::memory_order_relaxed);}
    std::atomic<ULONG> references_{1};
    std::shared_ptr<RouteSignal> signal_;
};
struct EndpointRoute {
    std::shared_ptr<RouteSignal> signal=std::make_shared<RouteSignal>();
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<EndpointNotifications> notifications;
    bool registered=false;
    bool Default(ComPtr<IMMDevice>& device,std::wstring& id){
        if(!devices){
            if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices))))return false;
            notifications.Attach(new EndpointNotifications(signal));
            // MMDevice does not retain this callback; keep our reference until
            // it is unregistered, and release all COM objects on this worker.
            registered=SUCCEEDED(devices->RegisterEndpointNotificationCallback(notifications.Get()));
        }
        if(FAILED(devices->GetDefaultAudioEndpoint(eRender,eConsole,&device)))return false;
        LPWSTR rawId=nullptr;
        const HRESULT result=device->GetId(&rawId);
        if(SUCCEEDED(result)&&rawId)id=rawId;
        CoTaskMemFree(rawId);
        return SUCCEEDED(result)&&!id.empty();
    }
    void Close(){
        if(registered)devices->UnregisterEndpointNotificationCallback(notifications.Get());
        registered=false;notifications.Reset();devices.Reset();
    }
    ~EndpointRoute(){Close();}
};

struct Stream {
    ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
    unsigned rate=0,channels=0;bool floating=false;unsigned bits=0;
    bool Open(IMMDevice* device){
        if(!device||FAILED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf()))))
            return false;
        WAVEFORMATEX* format=nullptr;
        if(FAILED(client->GetMixFormat(&format))||!format)return false;
        rate=format->nSamplesPerSec;channels=format->nChannels;bits=format->wBitsPerSample;
        floating=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT||
            (format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&
             reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const bool supported=channels>0&&rate>0&&((floating&&bits==32)||(!floating&&bits==16));
        const HRESULT hr=supported?client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,
            1000000,0,format,nullptr):E_FAIL;
        CoTaskMemFree(format);
        return SUCCEEDED(hr)&&SUCCEEDED(client->GetService(IID_PPV_ARGS(&capture)))&&SUCCEEDED(client->Start());
    }
    void Close(){if(client)client->Stop();capture.Reset();client.Reset();rate=channels=bits=0;floating=false;}
};
}
struct SpectrumCapture::State {
    explicit State(std::function<void()> notify):notify(std::move(notify)){}
    ~State(){Stop();}
    bool Start(){
        if(worker.joinable())return true;
        stop=false;idle=false;
        worker=std::thread([this]{Run();});
        return true;
    }
    void Stop(){
        {std::lock_guard lock(mutex);stop=true;}
        wake.notify_all();
        if(worker.joinable())worker.join();
        std::lock_guard lock(mutex);latest={};
    }
    void Run(){
        const bool com=SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        // Left and right are kept apart: the bars show them mirrored, as in
        // the reference (cava's stereo layout). Mono plays on both sides;
        // more channels contribute their front left and right.
        std::vector<float> left(spectrum::FrameSize,0.f),right(spectrum::FrameSize,0.f);std::size_t head=0;
        EndpointRoute route;ComPtr<IMMDevice> device;std::wstring deviceId;
        std::uint64_t observedRevision=0;
        Stream stream;bool open=false;
        auto lastData=std::chrono::steady_clock::now()-IdleAfter,nextOpen=std::chrono::steady_clock::now();
        auto nextRouteCheck=nextOpen;
        const auto clearSamples=[&](const auto& now){
            std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);
            head=0;lastData=now-IdleAfter-Interval;idle=false;
        };
        std::unique_lock lock(mutex);
        while(!stop){
            if(!active){
                latest={};
                lock.unlock();stream.Close();open=false;device.Reset();deviceId.clear();
                clearSamples(std::chrono::steady_clock::now());
                lock.lock();
                wake.wait(lock,[this]{return stop||active;});
                if(stop)break;
                nextOpen=nextRouteCheck=std::chrono::steady_clock::now();
            }
            lock.unlock();
            const auto now=std::chrono::steady_clock::now();
            const auto revision=route.signal->revision.load(std::memory_order_relaxed);
            if(now>=nextRouteCheck||revision!=observedRevision){
                // Observe before querying: a change during discovery/open must
                // still trigger another check on the next iteration.
                observedRevision=revision;nextRouteCheck=now+RouteCheckInterval;
                ComPtr<IMMDevice> current;std::wstring currentId;
                if(route.Default(current,currentId)){
                    if(currentId!=deviceId){
                        stream.Close();open=false;clearSamples(now);nextOpen=now;
                    }
                    device=std::move(current);deviceId=std::move(currentId);
                } else {
                    if(open||!deviceId.empty()){stream.Close();open=false;clearSamples(now);}
                    device.Reset();deviceId.clear();
                }
            }
            if(!open&&device&&now>=nextOpen){
                open=stream.Open(device.Get());
                if(!open){stream.Close();nextOpen=now+std::chrono::seconds(2);}
            }
            bool heard=false,lost=false;
            while(open){
                UINT32 packet=0;
                HRESULT hr=stream.capture->GetNextPacketSize(&packet);
                if(FAILED(hr)){lost=true;break;}
                if(!packet)break;
                BYTE* data=nullptr;UINT32 frames=0;DWORD flags=0;
                hr=stream.capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr);
                if(FAILED(hr)){lost=true;break;}
                const bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
                const auto sample=[&](UINT32 i,unsigned c){
                    if(stream.floating){float v;std::memcpy(&v,data+(i*stream.channels+c)*4,4);return v;}
                    std::int16_t v;std::memcpy(&v,data+(i*stream.channels+c)*2,2);return static_cast<float>(v)/32768.f;
                };
                for(UINT32 i=0;i<frames;++i){
                    left[head]=silent?0.f:sample(i,0);
                    right[head]=silent?0.f:sample(i,stream.channels>1?1:0);
                    head=(head+1)%left.size();
                }
                hr=stream.capture->ReleaseBuffer(frames);
                if(FAILED(hr)){lost=true;break;}
                if(!silent)heard=true;
            }
            // Device changed or vanished: reopen the new default shortly.
            if(lost){stream.Close();open=false;clearSamples(now);heard=false;nextOpen=now+std::chrono::milliseconds(500);}
            if(heard)lastData=now;
            if(now-lastData>SilenceAfter){std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);}
            const bool audible=now-lastData<=IdleAfter;
            if(audible||!idle){
                const auto ordered=[&](const std::vector<float>& ring){
                    std::vector<float> frame(ring.size());
                    for(std::size_t i=0;i<ring.size();++i)frame[i]=ring[(head+i)%ring.size()];
                    return frame;
                };
                Frame next{spectrum::Magnitudes(ordered(left)),spectrum::Magnitudes(ordered(right)),stream.rate?stream.rate:48000};
                {std::lock_guard guard(mutex);latest=std::move(next);}
                // One last frame of silence lets the bars settle, then rest.
                idle=!audible;
                if(notify)notify();
            }
            lock.lock();
            wake.wait_for(lock,Interval,[this]{return stop||!active;});
        }
        lock.unlock();
        stream.Close();device.Reset();route.Close();
        if(com)CoUninitialize();
    }
    std::function<void()> notify;
    mutable std::mutex mutex;std::condition_variable wake;bool stop=false,idle=false,active=true;
    Frame latest;std::thread worker;
};
SpectrumCapture::SpectrumCapture(std::function<void()> notify):state_(std::make_unique<State>(std::move(notify))){}
SpectrumCapture::~SpectrumCapture()=default;
bool SpectrumCapture::Start(){return state_->Start();}
void SpectrumCapture::SetActive(bool active){
    {std::lock_guard lock(state_->mutex);state_->active=active;}
    state_->wake.notify_all();
}
void SpectrumCapture::Stop(){state_->Stop();}
bool SpectrumCapture::Running()const{return state_->worker.joinable();}
SpectrumCapture::Frame SpectrumCapture::Latest()const{std::lock_guard lock(state_->mutex);return state_->latest;}
}
