#include "platform/windows/ui_integrity.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <fstream>

namespace lume::platform::windows {
namespace {
#include "ui_integrity_manifest.h"
namespace fs=std::filesystem;
std::wstring Native(const fs::path& path){
    auto text=path.wstring();std::replace(text.begin(),text.end(),L'/',L'\\');
    if(text.rfind(L"\\\\?\\",0)==0)return text;
    if(text.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+text.substr(2);
    return L"\\\\?\\"+text;
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE file):value(file){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    Hash(){
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0)
            BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    }
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    bool Add(const void* data,ULONG length){return hash&&BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(data)),length,0)>=0;}
    std::string Finish(){
        std::array<UCHAR,32> digest{};
        if(!hash||BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return {};
        std::string text;constexpr char hex[]="0123456789abcdef";
        for(const auto byte:digest){text+=hex[byte>>4];text+=hex[byte&15];}return text;
    }
};
bool Relative(const UiIntegrityEntry& entry){
    if(entry.relative.empty()||entry.relative.size()>240||entry.relative.find_first_of(L"\\:")!=entry.relative.npos||
       entry.sha256.size()!=64||entry.sha256.find_first_not_of("0123456789abcdef")!=entry.sha256.npos||entry.bytes>64ULL*1024*1024)return false;
    const fs::path path(entry.relative);
    if(path.is_absolute()||path.has_root_path())return false;
    for(const auto& part:path)if(part.empty()||part==L"."||part==L".."||part.native().back()==L'.'||part.native().back()==L' ')return false;
    return true;
}
bool Directory(const fs::path& root,bool create){
    if(!root.is_absolute())return false;
    auto current=root.root_path();
    for(const auto& part:root.relative_path()){
        if(part==L"."||part==L"..")return false;
        current/=part;
        auto attributes=GetFileAttributesW(Native(current).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES&&create){
            if(!CreateDirectoryW(Native(current).c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            attributes=GetFileAttributesW(Native(current).c_str());
        }
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return true;
}
bool Matches(const fs::path& root,const UiIntegrityEntry& entry){
    const auto path=root/fs::path(entry.relative);
    if(!Directory(path.parent_path(),false))return false;
    Handle file(CreateFileW(Native(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(file.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&info)||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)!=entry.bytes)return false;
    Hash hash;std::array<unsigned char,65536> buffer{};DWORD read=0;
    for(;;){
        if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))return false;
        if(!read)break;
        if(!hash.Add(buffer.data(),read))return false;
    }
    return hash.Finish()==entry.sha256;
}
bool CopyVerified(const fs::path& source,const fs::path& cache,const UiIntegrityEntry& entry){
    const auto from=source/fs::path(entry.relative),to=cache/fs::path(entry.relative);
    if(!Directory(from.parent_path(),false)||!Directory(to.parent_path(),true))return false;
    // Bound the snapshot before writing. It remains pinned against concurrent
    // write/delete and is checked again before the atomic destination rename.
    Handle input(CreateFileW(Native(from).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(input.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(input.value,&info)||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)!=entry.bytes)return false;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(entry.bytes));DWORD count=0;
    if(!bytes.empty()&&(!ReadFile(input.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)||count!=bytes.size()))return false;
    Hash hash;
    if(!hash.Add(bytes.data(),static_cast<ULONG>(bytes.size()))||hash.Finish()!=entry.sha256)return false;
    const auto temporary=to.wstring()+L".repair-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    bool written=false;
    {
        Handle output(CreateFileW(Native(temporary).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr));
        if(output.value==INVALID_HANDLE_VALUE)return false;
        written=WriteFile(output.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&
            count==bytes.size()&&FlushFileBuffers(output.value);
    }
    if(!written){DeleteFileW(Native(temporary).c_str());return false;}
    const auto attributes=GetFileAttributesW(Native(to).c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){DeleteFileW(Native(temporary).c_str());return false;}
    if(!MoveFileExW(Native(temporary).c_str(),Native(to).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(Native(temporary).c_str());return false;
    }
    return Matches(cache,entry);
}
}
const std::vector<UiIntegrityEntry>& BuiltUiManifest(){return UiIntegrityEntries;}
const char* BuiltUiIdentity(){return UiIntegrityId;}
UiIntegrityResult PrepareUiDirectory(const fs::path& ui,const fs::path& backup,const fs::path& cache,
    const std::vector<UiIntegrityEntry>& entries){
    UiIntegrityResult result;
    if(entries.empty()){result.failedFile=L"UI manifest";return result;}
    for(const auto& entry:entries){
        if(!Relative(entry)){result.failedFile=L"UI manifest";return result;}
        if(!Matches(ui,entry)){
            if(result.failedFile.empty())result.failedFile=entry.relative;
            ++result.damaged;
        }
    }
    if(!result.damaged){result.directory=ui;return result;}
    // The installer already flushed and verified this independent copy. Prefer
    // it directly when intact: recovery must not copy the entire asset tree at boot.
    if(std::all_of(entries.begin(),entries.end(),[&](const auto& entry){return Matches(backup,entry);})){result.directory=backup;result.recovered=true;result.failedFile.clear();return result;}
    if(cache.empty())return result;
    const bool cached=std::all_of(entries.begin(),entries.end(),[&](const auto& entry){return Matches(cache,entry);});
    if(!cached){
        if(!Directory(cache,true))return result;
        for(const auto& entry:entries){
            if(Matches(cache,entry))continue;
            const auto source=Matches(ui,entry)?ui:backup;
            if(!Matches(source,entry)||!CopyVerified(source,cache,entry)){result.failedFile=entry.relative;return result;}
        }
    }
    result.directory=cache;result.recovered=true;result.failedFile.clear();return result;
}
}
