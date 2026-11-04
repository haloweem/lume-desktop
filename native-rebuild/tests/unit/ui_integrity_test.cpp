#include "platform/windows/ui_integrity.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace {
namespace fs=std::filesystem;
using namespace lume::platform::windows;
void Check(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
void Zero(const fs::path& root,const UiIntegrityEntry& entry){
    std::ofstream output(root/entry.relative,std::ios::binary|std::ios::trunc);
    const std::string bytes(static_cast<std::size_t>(entry.bytes),'\0');output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    Check(output.good(),"write explicit corruption fixture");
}
}
int main(){
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto built=fs::path(executable).parent_path();
    const auto root=built/L"verification"/L"ui-integrity"/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto ui=root/L"ui",backup=root/L"ui-recovery",cache=root/L"private-cache";
    std::vector<UiIntegrityEntry> entries;
    for(const auto name:{L"index.html",L"localization.js",L"music-collections.js",L"music-visualizers/reference/preference-protocol.js"}){
        const auto entry=std::find_if(BuiltUiManifest().begin(),BuiltUiManifest().end(),[&](const auto& value){return value.relative==name;});
        Check(entry!=BuiltUiManifest().end(),"regression file is in the compiled manifest");entries.push_back(*entry);
        for(const auto& target:{ui,backup}){
            fs::create_directories((target/name).parent_path());fs::copy_file(built/L"ui"/name,target/name);
        }
    }
    auto result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.directory==ui&&!result.recovered&&!fs::exists(cache),"healthy UI needs no recovery cache or writes");
    for(std::size_t index=1;index<entries.size();++index)Zero(ui,entries[index]);
    result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.directory==backup&&result.recovered&&result.damaged==3&&!fs::exists(cache),"the three real all-zero scripts recover immediately from the exact-version backup without copying");
    Zero(backup,entries[0]);
    result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.directory==cache&&result.recovered,"partly damaged independent trees can build a fully verified private mirror");
    Check(PrepareUiDirectory(cache,backup,{},entries).directory==cache,"all recovered bytes match compiled SHA-256 digests");
    const auto stamp=fs::last_write_time(cache/entries[1].relative);
    result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.recovered&&fs::last_write_time(cache/entries[1].relative)==stamp,"reopening reuses the verified cache without writing again");
    Zero(cache,entries[1]);result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.directory==cache&&PrepareUiDirectory(cache,backup,{},entries).directory==cache,"a damaged cache is repaired before use");
    Zero(backup,entries[1]);result=PrepareUiDirectory(ui,backup,root/L"new-cache",entries);
    Check(result.directory.empty()&&result.failedFile==entries[1].relative,"damaged or different-version backup is never trusted");
    result=PrepareUiDirectory(ui,backup,cache,entries);
    Check(result.recovered,"a verified cache remains usable if the loose recovery copy is damaged");
    const auto longCache=root/std::wstring(120,L'x')/L"long-cache";
    fs::copy_file(built/L"ui"/entries[1].relative,backup/entries[1].relative,fs::copy_options::overwrite_existing);
    Check(PrepareUiDirectory(ui,backup,longCache,entries).directory==longCache,"long installation and cache paths support durable temporary files");
    auto invalid=entries;invalid[0].relative=L"../settings.json";
    Check(PrepareUiDirectory(ui,backup,root/L"escape-cache",invalid).directory.empty()&&!fs::exists(root/L"settings.json"),"manifest paths cannot escape the dedicated UI tree");
    Check(PrepareUiDirectory(ui,backup,{},entries).directory.empty(),"failed cache availability does not launch unverified scripts");
    std::cout<<"PASS UI integrity: real zero-file regression, exact hashes, cache repair/reuse, bad backup and path boundary; manifest="<<BuiltUiManifest().size()<<"\n";
}
