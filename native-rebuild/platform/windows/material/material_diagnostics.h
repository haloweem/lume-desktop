#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace lume::platform::windows::material_diagnostics {
// Shared by the shell-side lease and the DLL without linking their lifetimes.
// No titles, paths, media URLs or capture pixels enter this diagnostic stream.
inline void Write(std::string_view stage,HRESULT result,std::string_view details={}) noexcept {
    try {
        wchar_t path[32768]{};
        const auto length=GetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG",path,32768);
        if(!length||length>=32768)return;
        static std::mutex mutex;
        static ULONGLONG interval=0;
        static unsigned events=0,failures=0,lifecycleEvents=0;
        std::lock_guard<std::mutex> lock(mutex);
        const auto now=GetTickCount64();
        if(now-interval>=60000){interval=now;events=failures=lifecycleEvents=0;}
        // Successful startup stages cannot exhaust the error budget.
        // Capture retirement must retain its begin/end evidence even when
        // repeated component starts have already spent the startup budget.
        if(stage=="CaptureCloseBegin"||stage=="CaptureCloseEnd"){
            if(lifecycleEvents++>=64)return;
        }
        else if(FAILED(result)){if(failures++>=48)return;}
        else if(events++>=48)return;
        std::error_code ignored;
        const std::filesystem::path file(path);
        if(std::filesystem::file_size(file,ignored)>1024*1024&&!ignored) {
            const auto previous=file.parent_path()/L"material.1.log";
            if(!MoveFileExW(file.c_str(),previous.c_str(),MOVEFILE_REPLACE_EXISTING))return;
        }
        SYSTEMTIME time{};GetLocalTime(&time);
        char line[768]{};
        const int count=std::snprintf(line,sizeof(line),
            "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu %.*s hr=0x%08lx %.*s\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            GetCurrentProcessId(),GetCurrentThreadId(),static_cast<int>((std::min)(stage.size(),std::size_t{64})),stage.data(),
            static_cast<unsigned long>(result),static_cast<int>((std::min)(details.size(),std::size_t{480})),details.empty()?"":details.data());
        if(count<=0)return;
        const HANDLE handle=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return;
        DWORD written=0;WriteFile(handle,line,static_cast<DWORD>((std::min)(count,static_cast<int>(sizeof(line)-1))),&written,nullptr);
        CloseHandle(handle);
    }catch(...){}
}
}
