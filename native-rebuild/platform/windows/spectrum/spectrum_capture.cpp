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
#include <cstring>
#include <mutex>
#include <thread>
namespace lume::platform::windows {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto Interval=std::chrono::milliseconds(16);  // about 60 frames a second
// Without packets for this long the output is silent: let the bars fall.
constexpr auto SilenceAfter=std::chrono::milliseconds(150);
// After this much silence stop waking the UI until sound returns.
constexpr auto IdleAfter=std::chrono::seconds(1);

struct Stream {
    ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
    unsigned rate=0,channels=0;bool floating=false;unsigned bits=0;
    bool Open(){
        ComPtr<IMMDeviceEnumerator> devices;ComPtr<IMMDevice> device;
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)))||
           FAILED(devices->GetDefaultAudioEndpoint(eRender,eConsole,&device))||
           FAILED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf()))))
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
    void Close(){if(client)client->Stop();capture.Reset();client.Reset();}
};
}
struct SpectrumCapture::State {
    explicit State(std::function<void()> notify):notify(std::move(notify)){}
    ~State(){Stop();}
    bool Start(){
        if(worker.joinable())return true;
        stop=false;
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
        Stream stream;bool open=false;
        auto lastData=std::chrono::steady_clock::now()-IdleAfter,nextOpen=std::chrono::steady_clock::now();
        std::unique_lock lock(mutex);
        while(!stop){
            lock.unlock();
            const auto now=std::chrono::steady_clock::now();
            if(!open&&now>=nextOpen){
                open=stream.Open();
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
                stream.capture->ReleaseBuffer(frames);
                if(!silent)heard=true;
            }
            // Device changed or vanished: reopen the new default shortly.
            if(lost){stream.Close();open=false;nextOpen=now+std::chrono::milliseconds(500);}
            if(heard)lastData=now;
            if(now-lastData>SilenceAfter){std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);}
            const bool active=now-lastData<=IdleAfter;
            if(active||!idle){
                const auto ordered=[&](const std::vector<float>& ring){
                    std::vector<float> frame(ring.size());
                    for(std::size_t i=0;i<ring.size();++i)frame[i]=ring[(head+i)%ring.size()];
                    return frame;
                };
                Frame next{spectrum::Magnitudes(ordered(left)),spectrum::Magnitudes(ordered(right)),stream.rate?stream.rate:48000};
                {std::lock_guard guard(mutex);latest=std::move(next);}
                // One last frame of silence lets the bars settle, then rest.
                idle=!active;
                if(notify)notify();
            }
            lock.lock();
            wake.wait_for(lock,Interval,[this]{return stop;});
        }
        lock.unlock();
        stream.Close();
        if(com)CoUninitialize();
    }
    std::function<void()> notify;
    mutable std::mutex mutex;std::condition_variable wake;bool stop=false,idle=false;
    Frame latest;std::thread worker;
};
SpectrumCapture::SpectrumCapture(std::function<void()> notify):state_(std::make_unique<State>(std::move(notify))){}
SpectrumCapture::~SpectrumCapture()=default;
bool SpectrumCapture::Start(){return state_->Start();}
void SpectrumCapture::Stop(){state_->Stop();}
bool SpectrumCapture::Running()const{return state_->worker.joinable();}
SpectrumCapture::Frame SpectrumCapture::Latest()const{std::lock_guard lock(state_->mutex);return state_->latest;}
}
