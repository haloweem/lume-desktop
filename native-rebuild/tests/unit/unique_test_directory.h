#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
// Preserve prior evidence even after Windows reuses a process identifier.
inline std::filesystem::path UniqueTestDirectory(const std::filesystem::path& parent,const std::string& prefix=""){
    std::filesystem::create_directories(parent);
    FILETIME time{};GetSystemTimeAsFileTime(&time);
    const auto stamp=(static_cast<unsigned long long>(time.dwHighDateTime)<<32)|time.dwLowDateTime;
    for(unsigned attempt=0;attempt<100;++attempt){
        const auto path=parent/(prefix+std::to_string(GetCurrentProcessId())+"-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        std::error_code error;if(std::filesystem::create_directory(path,error))return path;
        if(error)throw std::runtime_error("Could not create isolated test directory");
    }
    throw std::runtime_error("Could not reserve unique test directory");
}
