#pragma once
#include "platform/windows/material/material_api.h"
#include <windows.h>

namespace material_test {
// Detach retires captures asynchronously so a stalled Windows Close cannot
// block the UI. Still require actual resource release, within a fixed deadline.
template<class Pump>
bool Released(LumeMaterialStatisticsFn statistics,LumeMaterialStatisticsData& status,Pump&& pump) {
    const auto deadline=GetTickCount64()+3000;
    do {
        if(!statistics(&status))return false;
        if(status.users==0&&status.devices==0&&status.captures==0)return true;
        pump(10);
    }while(GetTickCount64()<deadline);
    return statistics(&status)&&status.users==0&&status.devices==0&&status.captures==0;
}
}
