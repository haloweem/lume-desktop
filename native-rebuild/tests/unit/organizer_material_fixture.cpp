// Test-only material ABI fixture. Build into an isolated test directory; never
// stage this DLL beside the production shell or into the installer payload.
#include "platform/windows/material/material_api.h"
#include <map>

namespace {
struct Material {HWND owner;LumeMaterialStatus status;};
std::map<LumeMaterialHandle,Material> materials;
LumeMaterialHandle nextHandle=1;
}
extern "C" __declspec(dllexport) LumeMaterialHandle __cdecl LumeMaterialAttach(const LumeMaterialParameters* parameters){
    if(!parameters||parameters->size!=sizeof(*parameters)||!IsWindow(parameters->owner))return 0;
    const auto handle=nextHandle++;
    LumeMaterialStatus status{};status.size=sizeof(status);
    materials.emplace(handle,Material{parameters->owner,status});
    return handle;
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialUpdate(LumeMaterialHandle handle,const LumeMaterialParameters* parameters){
    const auto found=materials.find(handle);
    return found!=materials.end()&&parameters&&parameters->owner==found->second.owner;
}
extern "C" __declspec(dllexport) void __cdecl LumeMaterialDetach(LumeMaterialHandle handle){
    materials.erase(handle);
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialInspect(LumeMaterialHandle handle,LumeMaterialStatus* status){
    const auto found=materials.find(handle);
    if(found==materials.end()||!status||status->size!=sizeof(*status))return 0;
    *status=found->second.status;return 1;
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialStatistics(LumeMaterialStatisticsData* result){
    if(!result||result->size!=sizeof(*result))return 0;
    result->users=static_cast<uint32_t>(materials.size());result->devices=0;result->captures=0;return 1;
}
extern "C" __declspec(dllexport) int __cdecl LumeOrganizerMaterialFixtureSetStatus(HWND owner,const LumeMaterialStatus* status){
    if(!status||status->size!=sizeof(*status))return 0;
    for(auto& entry:materials)if(entry.second.owner==owner){
        entry.second.status=*status;
        return PostMessageW(owner,LumeMaterialStatusChangedMessage,0,0)!=FALSE;
    }
    return 0;
}
