#include "platform/windows/material/widget_material_runtime.h"
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
    if(!length||length>=32768){loadError_=ERROR_BAD_PATHNAME;return false;}
    std::wstring dll(path,length);const auto slash=dll.find_last_of(L"\\/");
    if(slash==std::wstring::npos){loadError_=ERROR_BAD_PATHNAME;return false;}
    dll.resize(slash+1);dll+=L"LumeGlass.dll";
    module_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module_){loadError_=GetLastError();return false;}
    const auto attach=Resolve<LumeMaterialAttachFn>(module_,"LumeMaterialAttach");
    update_=Resolve<LumeMaterialUpdateFn>(module_,"LumeMaterialUpdate");
    detach_=Resolve<LumeMaterialDetachFn>(module_,"LumeMaterialDetach");
    inspect_=Resolve<LumeMaterialInspectFn>(module_,"LumeMaterialInspect");
    if(!attach||!update_||!detach_||!inspect_){Detach();attempted_=true;loadError_=ERROR_PROC_NOT_FOUND;return false;}
    handle_=attach(&p);parameters_=p;
    if(!handle_){Detach();attempted_=true;loadError_=ERROR_NOT_SUPPORTED;return false;}
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
    update_=nullptr;inspect_=nullptr;attempted_=false;loadError_=0;parameters_={};
    if(handle&&detach)detach(handle);
    if(module)FreeLibrary(module);
}
LumeMaterialStatus WidgetMaterialRuntime::Status() const {
    LumeMaterialStatus result{};result.size=sizeof(result);
    if(handle_&&inspect_&&inspect_(handle_,&result))return result;
    result.failed=attempted_;result.lastError=loadError_?static_cast<int32_t>(HRESULT_FROM_WIN32(loadError_)):0;return result;
}
WidgetMaterialState WidgetMaterialRuntime::State() const {
    if(!attempted_)return WidgetMaterialState::Detached;
    const auto status=Status();
    if(status.failed||!handle_)return WidgetMaterialState::TransparentFallback;
    if(status.frameReady&&status.frames)return WidgetMaterialState::Ready;
    return status.initialized?WidgetMaterialState::TransparentFallback:WidgetMaterialState::Starting;
}
}
