#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/material/material_diagnostics.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace lume::platform::windows {
namespace {
template<class T> T Resolve(HMODULE module,const char* name) {
    const FARPROC address=GetProcAddress(module,name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;
}
}
bool WidgetMaterialRuntime::Attach(const LumeMaterialParameters& p) {
    Detach();attempted_=true;
    struct NotifyOnReturn {HWND owner;~NotifyOnReturn(){if(IsWindow(owner))PostMessageW(owner,LumeMaterialStatusChangedMessage,0,0);}} notify{p.owner};
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length||length>=32768){loadError_=ERROR_BAD_PATHNAME;Status();return false;}
    std::wstring dll(path,length);const auto slash=dll.find_last_of(L"\\/");
    if(slash==std::wstring::npos){loadError_=ERROR_BAD_PATHNAME;Status();return false;}
    dll.resize(slash+1);dll+=L"LumeGlass.dll";
    module_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module_){loadError_=GetLastError();Status();return false;}
    const auto attach=Resolve<LumeMaterialAttachFn>(module_,"LumeMaterialAttach");
    update_=Resolve<LumeMaterialUpdateFn>(module_,"LumeMaterialUpdate");
    detach_=Resolve<LumeMaterialDetachFn>(module_,"LumeMaterialDetach");
    inspect_=Resolve<LumeMaterialInspectFn>(module_,"LumeMaterialInspect");
    if(!attach||!update_||!detach_||!inspect_){Detach();attempted_=true;loadError_=ERROR_PROC_NOT_FOUND;Status();return false;}
    handle_=attach(&p);parameters_=p;
    if(!handle_){Detach();attempted_=true;loadError_=ERROR_NOT_SUPPORTED;Status();return false;}
    Status();
    return true;
}
bool WidgetMaterialRuntime::Update(const LumeMaterialParameters& p) {
    if(!handle_||!update_)return false;
    if(std::memcmp(&parameters_,&p,sizeof(p))==0)return true;
    if(!update_(handle_,&p))return false;
    parameters_=p;return true;
}
void WidgetMaterialRuntime::Detach() {
    // Detach can service synchronous window messages. Retire our state before
    // entering the DLL, and keep its module alive until that call returns.
    const auto handle=std::exchange(handle_,0);
    const auto detach=std::exchange(detach_,nullptr);
    const auto module=std::exchange(module_,nullptr);
    update_=nullptr;inspect_=nullptr;attempted_=false;loadError_=0;parameters_={};diagnosticSeen_=false;
    if(handle&&detach)detach(handle);
    if(module)FreeLibrary(module);
}
LumeMaterialStatus WidgetMaterialRuntime::Status() const {
    LumeMaterialStatus result{};result.size=sizeof(result);
    if(!(handle_&&inspect_&&inspect_(handle_,&result))) {
        result.failed=attempted_;result.lastError=loadError_?static_cast<int32_t>(HRESULT_FROM_WIN32(loadError_)):0;
    }
    if(attempted_)RecordStatus(result);
    return result;
}
void WidgetMaterialRuntime::RecordStatus(const LumeMaterialStatus& status) const {
    const auto& prior=diagnosticPrevious_;
    if(diagnosticSeen_&&prior.initialized==status.initialized&&prior.frameReady==status.frameReady&&
       prior.failed==status.failed&&prior.lastError==status.lastError&&
       (prior.sources!=0)==(status.sources!=0)&&(prior.readySources!=0)==(status.readySources!=0))return;
    diagnosticSeen_=true;diagnosticPrevious_=status;
    wchar_t windowClass[96]{};GetClassNameW(parameters_.owner,windowClass,96);
    char asciiClass[96]{};
    for(std::size_t i=0;i<95&&windowClass[i];++i)asciiClass[i]=windowClass[i]>=32&&windowClass[i]<127?static_cast<char>(windowClass[i]):'?';
    char detail[384]{};
    std::snprintf(detail,sizeof(detail),"ownerClass=%s loadError=%lu initialized=%u frameReady=%u failed=%u frames=%u sources=%u readySources=%u scenePixels=%u surfacePixels=%u",
        asciiClass,loadError_,status.initialized,status.frameReady,status.failed,status.frames,status.sources,status.readySources,status.scenePixels,status.surfacePixels);
    material_diagnostics::Write(loadError_?"MaterialLoadFailure":"MaterialState",status.lastError,detail);
}
WidgetMaterialState WidgetMaterialRuntime::State() const {
    if(!attempted_)return WidgetMaterialState::Detached;
    const auto status=Status();
    if(status.failed||!handle_)return WidgetMaterialState::TransparentFallback;
    if(status.frameReady&&status.frames)return WidgetMaterialState::Ready;
    return status.initialized?WidgetMaterialState::TransparentFallback:WidgetMaterialState::Starting;
}
}
