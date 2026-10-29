#include "platform/windows/organizer/organizer_text.h"
#include <windows.h>
namespace lume::platform::windows {
std::wstring OrganizerWide(std::string_view input){
    if(input.empty()||input.size()>1024*1024)return {};
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0);
    if(size<=0)return {};
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),result.data(),size))return {};
    return result;
}
std::string OrganizerUtf8(std::wstring_view input){
    if(input.empty()||input.size()>1024*1024)return {};
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string result(static_cast<std::size_t>(size),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),result.data(),size,nullptr,nullptr))return {};
    return result;
}
namespace {
std::string Lower(std::wstring_view input,bool linguistic){
    if(input.empty())return {};
    const DWORD flags=LCMAP_LOWERCASE|(linguistic?LCMAP_LINGUISTIC_CASING:0);
    const int size=LCMapStringEx(LOCALE_NAME_INVARIANT,flags,input.data(),static_cast<int>(input.size()),nullptr,0,nullptr,nullptr,0);
    if(size<=0)return {};
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,flags,input.data(),static_cast<int>(input.size()),result.data(),size,nullptr,nullptr,0))return {};
    return OrganizerUtf8(result);
}
}
std::string OrganizerPathKey(std::string_view input){return Lower(OrganizerWide(input),false);}
std::string OrganizerSearchKey(std::string_view input){
    const auto wide=OrganizerWide(input);if(wide.empty())return {};
    int size=NormalizeString(NormalizationKC,wide.data(),static_cast<int>(wide.size()),nullptr,0);
    // Windows returns an estimate, and a negative revised size if it is short.
    for(unsigned attempt=0;attempt<3&&size>0&&size<=1024*1024;++attempt){
        std::wstring normalized(static_cast<std::size_t>(size),L'\0');
        const int written=NormalizeString(NormalizationKC,wide.data(),static_cast<int>(wide.size()),normalized.data(),size);
        if(written>0){normalized.resize(static_cast<std::size_t>(written));return Lower(normalized,true);}
        if(written>=0||GetLastError()!=ERROR_INSUFFICIENT_BUFFER)return {};
        size=-written;
    }
    return {};
}
}
