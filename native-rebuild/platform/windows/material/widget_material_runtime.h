#pragma once
#include "platform/windows/material/material_api.h"

namespace lume::platform::windows {
enum class WidgetMaterialState { Detached, Starting, Ready, TransparentFallback };
// Reusable per-window lease; GPU devices and WGC sessions are shared inside the DLL.
class WidgetMaterialRuntime final {
public:
    WidgetMaterialRuntime()=default;
    ~WidgetMaterialRuntime(){Detach();}
    WidgetMaterialRuntime(const WidgetMaterialRuntime&)=delete;
    WidgetMaterialRuntime& operator=(const WidgetMaterialRuntime&)=delete;
    bool Attach(const LumeMaterialParameters&);
    bool Update(const LumeMaterialParameters&);
    void Detach();
    WidgetMaterialState State() const;
    LumeMaterialStatus Status() const;
private:
    HMODULE module_=nullptr;
    LumeMaterialHandle handle_=0;
    LumeMaterialUpdateFn update_=nullptr;
    LumeMaterialDetachFn detach_=nullptr;
    LumeMaterialInspectFn inspect_=nullptr;
    LumeMaterialParameters parameters_{};
    DWORD loadError_=0;
    bool attempted_=false;
};
}
