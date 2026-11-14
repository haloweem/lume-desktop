// Exercise the production capture worker through fake WASAPI interfaces. This
// executable never opens real audio devices, plays sound, or changes defaults.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace spectrum_capture_test {
using Clock=std::chrono::steady_clock;
constexpr UINT32 PacketFrames=1024;
constexpr unsigned SampleRate=48000;
constexpr wchar_t EndpointA[]=L"spectrum-test-endpoint-A";
constexpr wchar_t EndpointB[]=L"spectrum-test-endpoint-B";
const wchar_t* Id(int endpoint){return endpoint==0?EndpointA:endpoint==1?EndpointB:nullptr;}
struct Environment {
    std::atomic<int> selected{0};
    std::atomic<bool> sounding[2]{{true},{false}};
    std::atomic<bool> failPacket[2]{},failBuffer[2]{},failRelease[2]{};
    std::atomic<bool> failActivate[2]{};
    std::atomic<bool> failRegistration{false};
    std::atomic<bool> switchDuringQuery{false};
    std::atomic<unsigned> queries{0},notifications{0},registrations{0},unregistrations{0};
    std::atomic<unsigned> activations[2]{},packets[2]{},zeroPolls[2]{};
    std::atomic<unsigned> activationAttempts[2]{},activationFailures[2]{},querySwitches{0};
    std::atomic<unsigned> liveObjects{0},unexpectedFactories{0};
} env;
struct Registration {void* owner;IMMNotificationClient* callback;};
std::mutex callbackMutex;
std::vector<Registration> callbacks;

void RemoveCallbacks(void* owner,IMMNotificationClient* callback=nullptr){
    std::lock_guard lock(callbackMutex);
    callbacks.erase(std::remove_if(callbacks.begin(),callbacks.end(),[&](const auto& item){
        return item.owner==owner&&(!callback||item.callback==callback);
    }),callbacks.end());
}
unsigned CallbackCount(){std::lock_guard lock(callbackMutex);return static_cast<unsigned>(callbacks.size());}
void NotifyDefault(EDataFlow flow=eRender,ERole role=eConsole){
    std::vector<IMMNotificationClient*> snapshot;
    {
        std::lock_guard lock(callbackMutex);
        for(const auto& item:callbacks){item.callback->AddRef();snapshot.push_back(item.callback);}
    }
    // Calls occur without the registry mutex, like an independent COM callback.
    for(auto* item:snapshot){item->OnDefaultDeviceChanged(flow,role,Id(env.selected.load()));item->Release();}
}
void Select(int endpoint,bool notify=true){env.selected=endpoint;if(notify)NotifyDefault();}

#define SPECTRUM_TEST_COM(Interface) \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override { \
        if(!out)return E_POINTER;*out=nullptr; \
        if(iid==__uuidof(IUnknown)||iid==__uuidof(Interface)){ \
            *out=static_cast<Interface*>(this);AddRef();return S_OK;}return E_NOINTERFACE; } \
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;} \
    ULONG STDMETHODCALLTYPE Release() override {const ULONG count=--refs_;if(!count)delete this;return count;} \
    std::atomic<ULONG> refs_{1}

class FakeCapture final:public IAudioCaptureClient {
public:
    explicit FakeCapture(int endpoint):endpoint_(endpoint),data_(PacketFrames*2){++env.liveObjects;}
    ~FakeCapture(){--env.liveObjects;}
    SPECTRUM_TEST_COM(IAudioCaptureClient);
    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** data,UINT32* frames,DWORD* flags,UINT64* position,UINT64* qpc) override {
        if(env.failBuffer[endpoint_].exchange(false))return AUDCLNT_E_DEVICE_INVALIDATED;
        if(!data||!frames||!flags)return E_POINTER;
        constexpr double Pi=3.14159265358979323846;
        const double frequency=endpoint_==0?750.0:1500.0;
        for(UINT32 i=0;i<PacketFrames;++i){
            const float value=static_cast<float>(0.6*std::sin(2.0*Pi*frequency*static_cast<double>(offset_+i)/SampleRate));
            data_[i*2]=value;data_[i*2+1]=value;
        }
        *data=reinterpret_cast<BYTE*>(data_.data());*frames=PacketFrames;*flags=0;
        if(position)*position=offset_;if(qpc)*qpc=0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 frames) override {
        offset_+=frames;nextPacket_=Clock::now()+std::chrono::milliseconds(16);
        if(env.failRelease[endpoint_].exchange(false))return AUDCLNT_E_DEVICE_INVALIDATED;
        ++env.packets[endpoint_];return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32* count) override {
        if(env.failPacket[endpoint_].exchange(false))return AUDCLNT_E_DEVICE_INVALIDATED;
        if(!count)return E_POINTER;
        *count=env.sounding[endpoint_].load()&&Clock::now()>=nextPacket_?PacketFrames:0;
        if(!*count)++env.zeroPolls[endpoint_];
        return S_OK;
    }
private:
    int endpoint_;
    std::vector<float> data_;
    UINT64 offset_=0;
    Clock::time_point nextPacket_=Clock::now();
};

class FakeAudio final:public IAudioClient {
public:
    explicit FakeAudio(int endpoint):capture_(new FakeCapture(endpoint)){++env.liveObjects;}
    ~FakeAudio(){capture_->Release();--env.liveObjects;}
    SPECTRUM_TEST_COM(IAudioClient);
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE mode,DWORD flags,REFERENCE_TIME,REFERENCE_TIME,const WAVEFORMATEX*,LPCGUID) override {
        return mode==AUDCLNT_SHAREMODE_SHARED&&(flags&AUDCLNT_STREAMFLAGS_LOOPBACK)?S_OK:E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* value) override {if(!value)return E_POINTER;*value=PacketFrames;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME* value) override {if(!value)return E_POINTER;*value=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* value) override {if(!value)return E_POINTER;*value=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE,const WAVEFORMATEX*,WAVEFORMATEX**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** format) override {
        if(!format)return E_POINTER;
        *format=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
        if(!*format)return E_OUTOFMEMORY;
        **format={};(*format)->wFormatTag=WAVE_FORMAT_IEEE_FLOAT;(*format)->nChannels=2;
        (*format)->nSamplesPerSec=SampleRate;(*format)->wBitsPerSample=32;
        (*format)->nBlockAlign=8;(*format)->nAvgBytesPerSec=SampleRate*8;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME*,REFERENCE_TIME*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Start() override {return S_OK;}
    HRESULT STDMETHODCALLTYPE Stop() override {return S_OK;}
    HRESULT STDMETHODCALLTYPE Reset() override {return S_OK;}
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetService(REFIID iid,void** out) override {return capture_->QueryInterface(iid,out);}
private:
    FakeCapture* capture_;
};

class FakeDevice final:public IMMDevice {
public:
    explicit FakeDevice(int endpoint):endpoint_(endpoint){++env.liveObjects;}
    ~FakeDevice(){--env.liveObjects;}
    SPECTRUM_TEST_COM(IMMDevice);
    HRESULT STDMETHODCALLTYPE Activate(REFIID iid,DWORD,PROPVARIANT*,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IAudioClient))return E_NOINTERFACE;
        ++env.activationAttempts[endpoint_];
        if(env.failActivate[endpoint_].exchange(false)){
            ++env.activationFailures[endpoint_];return AUDCLNT_E_DEVICE_INVALIDATED;
        }
        *out=static_cast<IAudioClient*>(new FakeAudio(endpoint_));++env.activations[endpoint_];return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD,IPropertyStore**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override {
        if(!out)return E_POINTER;
        const wchar_t* value=Id(endpoint_);const auto bytes=(std::wcslen(value)+1)*sizeof(wchar_t);
        *out=static_cast<LPWSTR>(CoTaskMemAlloc(bytes));if(!*out)return E_OUTOFMEMORY;
        std::memcpy(*out,value,bytes);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(DWORD* state) override {if(!state)return E_POINTER;*state=DEVICE_STATE_ACTIVE;return S_OK;}
private:
    int endpoint_;
};

class FakeEnumerator final:public IMMDeviceEnumerator {
public:
    FakeEnumerator(){++env.liveObjects;}
    ~FakeEnumerator(){RemoveCallbacks(this);--env.liveObjects;}
    SPECTRUM_TEST_COM(IMMDeviceEnumerator);
    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow,DWORD,IMMDeviceCollection**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow flow,ERole role,IMMDevice** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(flow!=eRender||role!=eConsole)return E_INVALIDARG;
        ++env.queries;const int selected=env.selected.load();
        if(selected<0)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if(env.switchDuringQuery.exchange(false)){
            env.sounding[0]=false;env.sounding[1]=true;Select(1);++env.querySwitches;
        }
        // Return the endpoint discovered before the injected concurrent change.
        *out=new FakeDevice(selected);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR id,IMMDevice** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(!id)return E_INVALIDARG;
        const int endpoint=std::wcscmp(id,EndpointA)==0?0:std::wcscmp(id,EndpointB)==0?1:-1;
        if(endpoint<0)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        *out=new FakeDevice(endpoint);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient* callback) override {
        if(!callback)return E_POINTER;if(env.failRegistration)return E_FAIL;
        // MMDevice registration does not retain the callback. Production owns
        // its lifetime; only an in-flight NotifyDefault snapshot takes a ref.
        std::lock_guard lock(callbackMutex);callbacks.push_back({this,callback});
        ++env.registrations;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient* callback) override {
        RemoveCallbacks(this,callback);++env.unregistrations;return S_OK;
    }
};
#undef SPECTRUM_TEST_COM

HRESULT FakeCoCreateInstance(REFCLSID clsid,LPUNKNOWN outer,DWORD,REFIID iid,LPVOID* out){
    if(!out)return E_POINTER;*out=nullptr;
    if(clsid!=__uuidof(MMDeviceEnumerator)||outer){++env.unexpectedFactories;return E_NOTIMPL;}
    auto* enumerator=new FakeEnumerator;
    const HRESULT result=enumerator->QueryInterface(iid,out);enumerator->Release();return result;
}
}

// System declarations are already included. Only this production call site is
// redirected; the worker, frame clearing, FFT publication, and lifecycle are real.
#define CoCreateInstance spectrum_capture_test::FakeCoCreateInstance
#include "platform/windows/spectrum/spectrum_capture.cpp"
#undef CoCreateInstance

namespace spectrum_capture_test {
using Capture=lume::platform::windows::SpectrumCapture;
struct Peak {float value=0;std::size_t bin=0;};
Peak ReadPeak(const Capture& capture){
    const auto frame=capture.Latest();Peak result;
    for(std::size_t i=0;i<frame.left.size();++i)if(frame.left[i]>result.value)result={frame.left[i],i};
    return result;
}
bool HasTone(const Capture& capture,int endpoint){
    const auto peak=ReadPeak(capture);return peak.value>0.01f&&peak.bin==(endpoint==0?128U:256U);
}
bool Wait(const std::function<bool()>& predicate,int milliseconds=2500){
    const auto end=Clock::now()+std::chrono::milliseconds(milliseconds);
    do {if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(Clock::now()<end);
    return predicate();
}
unsigned assertions=0;
void Expect(bool ok,const char* message){
    if(!ok)throw std::runtime_error(message);
    ++assertions;std::cout<<"PASS "<<message<<'\n';
}
void VerifyNoObjects(){Expect(env.liveObjects==0&&CallbackCount()==0,"Stop releases all fake COM objects and callbacks");}

void ExerciseCapture(){
    Capture capture([]{++env.notifications;});
    Expect(capture.Start(),"capture starts without touching real audio");
    Expect(Wait([&]{return HasTone(capture,0);}),"A samples produce real FFT at A frequency");
    env.sounding[0]=false;
    Expect(Wait([&]{return ReadPeak(capture).value==0.f;}),"silence clears the existing FFT");
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    const auto idleNotifications=env.notifications.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Expect(env.notifications==idleNotifications,"idle silence stops frame notifications");
    const auto sameDeviceActivations=env.activations[0].load();
    env.sounding[0]=true;
    Expect(Wait([&]{return HasTone(capture,0);}),"same endpoint resumes after silent idle");
    Expect(env.activations[0]==sameDeviceActivations,"ordinary silence does not restart the audio stream");

    env.sounding[0]=false;env.sounding[1]=true;Select(1);
    Expect(Wait([&]{return HasTone(capture,1);}),"default A-to-B switch recovers while valid A returns zero packets");
    Expect(env.activations[1]==1&&CallbackCount()==1,"B activated once and default notifications registered");
    env.sounding[1]=false;env.sounding[0]=true;Select(0);
    Expect(Wait([&]{return HasTone(capture,0);}),"default B-to-A switch also recovers");

    const auto beforeA=env.activations[0].load(),beforeB=env.activations[1].load();
    NotifyDefault();NotifyDefault(eRender,eMultimedia);NotifyDefault(eRender,eCommunications);NotifyDefault(eCapture,eConsole);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    Expect(env.activations[0]==beforeA&&env.activations[1]==beforeB,"same default and unrelated role notifications do not restart streams");

    const auto releaseActivation=env.activations[0].load();env.failRelease[0]=true;
    Expect(Wait([&]{return env.activations[0]>releaseActivation&&HasTone(capture,0);}),"ReleaseBuffer failure reopens and restores capture");
    const auto packetActivation=env.activations[0].load();env.failPacket[0]=true;
    Expect(Wait([&]{return env.activations[0]>packetActivation&&HasTone(capture,0);}),"GetNextPacketSize failure reopens and restores capture");
    const auto bufferActivation=env.activations[0].load();env.failBuffer[0]=true;
    Expect(Wait([&]{return env.activations[0]>bufferActivation&&HasTone(capture,0);}),"GetBuffer failure reopens and restores capture");

    // A remains valid and sounding, but no endpoint is the current default.
    Select(-1);
    Expect(Wait([&]{return ReadPeak(capture).value==0.f;}),"removing the default clears stale FFT history");
    env.sounding[0]=false;env.sounding[1]=true;Select(1);
    Expect(Wait([&]{return HasTone(capture,1);},3500),"capture recovers when a default device appears again");

    env.sounding[1]=false;
    Expect(Wait([&]{return ReadPeak(capture).value==0.f;}),"capture becomes silent before stopping");
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    const auto unregisterBefore=env.unregistrations.load();capture.Stop();
    Expect(env.unregistrations==unregisterBefore+1&&CallbackCount()==0,"Stop unregisters its notification callback");
    Expect(!capture.Running()&&capture.Latest().left.empty(),"Stop joins the worker and clears Latest");
    VerifyNoObjects();NotifyDefault();
    env.sounding[1]=false;
    const auto restartNotifications=env.notifications.load();
    Expect(capture.Start(),"capture can restart on the same object");
    Expect(Wait([&]{return env.notifications>restartNotifications&&!capture.Latest().left.empty();}),"silent restart publishes an initial cleared frame");
    env.sounding[1]=true;
    Expect(Wait([&]{return HasTone(capture,1);}),"sound resumes after Stop and Start");
    capture.Stop();VerifyNoObjects();
}

void ExerciseRegistrationFallback(){
    env.failRegistration=true;env.selected=0;env.sounding[0]=true;env.sounding[1]=false;
    Capture capture([]{++env.notifications;});capture.Start();
    Expect(Wait([&]{return HasTone(capture,0);}),"capture still works if notification registration fails");
    Expect(CallbackCount()==0,"registration failure leaves no callback behind");
    env.sounding[0]=false;env.sounding[1]=true;Select(1,false);
    Expect(Wait([&]{return HasTone(capture,1);},3500),"periodic endpoint-ID check recovers missed default changes");
    capture.Stop();VerifyNoObjects();env.failRegistration=false;
}

void ExerciseTransientOpenFailure(){
    env.selected=0;env.sounding[0]=true;env.sounding[1]=false;env.failActivate[0]=true;
    const auto failuresBefore=env.activationFailures[0].load();
    Capture capture([]{++env.notifications;});capture.Start();
    Expect(Wait([&]{return env.activationFailures[0]>failuresBefore;}),"temporary Activate failure is exercised");
    const auto attemptsAfterFailure=env.activationAttempts[0].load();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Expect(env.activationAttempts[0]==attemptsAfterFailure,"failed open backs off rather than busy-retrying");
    Expect(Wait([&]{return HasTone(capture,0);},3500),"same endpoint ID retries a failed open without another notification");
    capture.Stop();VerifyNoObjects();
}

void ExerciseChangeDuringDiscovery(){
    env.selected=0;env.sounding[0]=true;env.sounding[1]=false;env.switchDuringQuery=true;
    const auto switchesBefore=env.querySwitches.load();
    Capture capture([]{++env.notifications;});capture.Start();
    Expect(Wait([&]{return env.querySwitches>switchesBefore;}),"default changes while discovery is returning old A");
    // Notification recovery should precede the one-second fallback check. The
    // event arrives from inside discovery after its old endpoint is captured.
    Expect(Wait([&]{return HasTone(capture,1);},800),"change during discovery is observed before periodic fallback");
    capture.Stop();VerifyNoObjects();
}
}

int main(){
    using namespace spectrum_capture_test;
    try {
        ExerciseCapture();ExerciseRegistrationFallback();
        ExerciseTransientOpenFailure();ExerciseChangeDuringDiscovery();
        Expect(env.unexpectedFactories==0,"no COM factory escaped the fake audio boundary");
        std::cout<<"spectrum capture: "<<assertions<<" assertions passed\n";return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL "<<error.what()<<" (after "<<assertions<<" passed assertions)\n";return 1;
    }
}
