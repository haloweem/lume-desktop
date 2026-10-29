#include "platform/windows/wallpaper/media_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
int failures=0;
void Expect(bool result,const char* why){if(!result){std::cerr<<"FAIL: "<<why<<'\n';++failures;}}
std::string Request(const std::string& url,const std::string& range={},bool head=false,const std::string& origin={}) {
    const auto colon=url.find(':',7),slash=url.find('/',colon);
    if(colon==std::string::npos||slash==std::string::npos)return {};
    SOCKET socketHandle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(socketHandle==INVALID_SOCKET)return {};
    DWORD timeout=3000;setsockopt(socketHandle,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(static_cast<unsigned short>(std::stoi(url.substr(colon+1,slash-colon-1))));
    if(connect(socketHandle,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0){closesocket(socketHandle);return {};}
    const std::string request=(head?"HEAD ":"GET ")+url.substr(slash)+" HTTP/1.1\r\nHost: 127.0.0.1\r\n"+
        (origin.empty()?"":"Origin: "+origin+"\r\n")+
        (range.empty()?"":"Range: "+range+"\r\n")+"Connection: close\r\n\r\n";
    send(socketHandle,request.data(),static_cast<int>(request.size()),0);
    std::string response;char buffer[4096];
    for(;;){const int count=recv(socketHandle,buffer,sizeof(buffer),0);if(count<=0)break;response.append(buffer,count);}
    closesocket(socketHandle);return response;
}

void TestLeaseRevocation() {
    using namespace lume::platform::windows;
    WallpaperMediaServer server;
    struct SlowMedia {std::atomic<unsigned> reads{0};};
    auto reader=std::make_shared<SlowMedia>();
    const std::weak_ptr<SlowMedia> lease=reader;
    WallpaperMediaSource slow;
    slow.bytes=4*1024*1024;slow.contentType="video/mp4";slow.extension="mp4";
    slow.read=[reader](std::uint64_t,char* output,std::size_t capacity,std::size_t& read) {
        ++reader->reads;
        Sleep(25);
        read=capacity;std::memset(output,'A',read);return true;
    };
    const auto first=server.Register("local-first",std::move(slow));
    WallpaperMediaSource other;
    other.bytes=8;other.contentType="video/mp4";other.extension="mp4";
    other.read=[](std::uint64_t offset,char* output,std::size_t capacity,std::size_t& read) {
        read=std::min<std::size_t>(capacity,8-static_cast<std::size_t>(offset));
        std::memset(output,'B',read);return true;
    };
    const auto second=server.Register("local-second",std::move(other));
    std::string received;
    auto slowClient=std::thread([&]{received=Request(first);});
    for(unsigned attempt=0;attempt<200&&!reader->reads;++attempt)Sleep(5);
    Expect(reader->reads>0,"first lease has an established streaming client");
    reader.reset();
    const auto before=GetTickCount64();
    server.Remove("local-first");
    Expect(GetTickCount64()-before<600,"single-lease revocation is bounded");
    Expect(lease.expired(),"Remove releases the revoked active reader before returning");
    slowClient.join();
    Expect(received.size()<4*1024*1024,"revoked stream cannot finish sending its media");
    const auto stillAvailable=Request(second);
    Expect(stillAvailable.find("200 OK")!=std::string::npos&&
        stillAvailable.substr(stillAvailable.find("\r\n\r\n")+4)=="BBBBBBBB",
        "revoking first lease does not interrupt the second lease");
    Expect(Request(first).find("404 Not Found")!=std::string::npos,"old capability cannot create another stream");
    server.Remove("local-second");
    Expect(server.Origin().empty()&&server.Clients()==0,"last lease releases all server ownership");
}

void TestStopDuringClientTraffic() {
    using namespace lume::platform::windows;
    WallpaperMediaServer server;
    for(unsigned iteration=0;iteration<12;++iteration) {
        WallpaperMediaSource source;
        source.bytes=65536;source.contentType="video/mp4";source.extension="mp4";
        source.read=[](std::uint64_t offset,char* output,std::size_t capacity,std::size_t& read) {
            read=std::min<std::size_t>(capacity,65536-static_cast<std::size_t>(offset));
            std::memset(output,'S',read);return true;
        };
        const auto url=server.Register("local-stress",std::move(source));
        Expect(!url.empty(),"server restarts after a complete Stop");
        std::atomic<bool> finished{false};
        auto requests=std::thread([&] {for(unsigned i=0;i<4&&!finished;++i)Request(url);});
        auto socketChurn=std::thread([&] {
            while(!finished) {
                const auto socketHandle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
                if(socketHandle!=INVALID_SOCKET)closesocket(socketHandle);
                Sleep(1);
            }
        });
        Sleep(2);
        const auto before=GetTickCount64();
        server.Stop();
        finished=true;
        requests.join();socketChurn.join();
        Expect(GetTickCount64()-before<1200,"Stop remains bounded with concurrent client socket closure");
        Expect(server.Origin().empty()&&server.Clients()==0,"Stop removes listener and all client workers");
    }
}
void TestExactOriginCors() {
    using namespace lume::platform::windows;
    for(const auto* invalid:{"*","https://lume.local/","https://lume.local/path","http://","javascript://lume.local",
                             "https://lume.local\r\nAccess-Control-Allow-Origin: *","https://user@lume.local",
                             "null","https://evil.test","http://lume.local","https://lume.local:443",
                             "http://127.0.0.1:0","http://127.0.0.1:65536","http://127.0.0.1:0123",
                             "http://127.0.0.1:123/path","http://127.0.0.1:123?query",
                             "http://127.0.0.1:123#fragment","http://127.0.0.1:123@evil.test"}) {
        bool rejected=false;
        try { WallpaperMediaServer server(invalid); } catch(const std::invalid_argument&) { rejected=true; }
        Expect(rejected,"invalid CORS origin rejected");
    }
    for(const auto* allowed:{"https://lume.local","http://127.0.0.1:45555"}) {
    WallpaperMediaServer server(allowed);
    const auto media=std::make_shared<std::string>("cors-fixture");
    WallpaperMediaSource source;
    source.bytes=media->size();source.contentType="video/mp4";source.extension="mp4";
    source.read=[media](std::uint64_t offset,char* output,std::size_t capacity,std::size_t& read) {
        if(offset>=media->size()){read=0;return true;}
        read=std::min(capacity,media->size()-static_cast<std::size_t>(offset));
        std::memcpy(output,media->data()+offset,read);return true;
    };
    const auto url=server.Register("local-cors",source);
    Expect(!url.empty(),"CORS fixture registered");
    const auto allowedGet=Request(url,{},false,allowed);
    Expect(allowedGet.find("Access-Control-Allow-Origin: "+std::string(allowed)+"\r\n")!=std::string::npos,
        "allowed GET receives exact ACAO");
    const auto allowedHead=Request(url,{},true,allowed);
    Expect(allowedHead.find("Access-Control-Allow-Origin: "+std::string(allowed)+"\r\n")!=std::string::npos&&
        allowedHead.substr(allowedHead.find("\r\n\r\n")+4).empty(),"allowed HEAD receives exact ACAO");
    const auto allowedRange=Request(url,"bytes=1-3",false,allowed);
    Expect(allowedRange.find("206 Partial Content")!=std::string::npos&&
        allowedRange.find("Access-Control-Allow-Origin: "+std::string(allowed)+"\r\n")!=std::string::npos&&
        allowedRange.substr(allowedRange.find("\r\n\r\n")+4)=="ors",
        "allowed range receives exact ACAO");
    const auto headRange=Request(url,"bytes=1-3",true,allowed);
    Expect(headRange.find("206 Partial Content")!=std::string::npos&&
        headRange.find("Access-Control-Allow-Origin: "+std::string(allowed)+"\r\n")!=std::string::npos&&
        headRange.substr(headRange.find("\r\n\r\n")+4).empty(),"HEAD range has CORS but no body");
    const auto invalidRange=Request(url,"bytes=99-",false,allowed);
    Expect(invalidRange.find("416 Range Not Satisfiable")!=std::string::npos&&
        invalidRange.find("Access-Control-Allow-Origin: "+std::string(allowed)+"\r\n")!=std::string::npos,
        "allowed unsatisfiable range retains CORS");
    for(const auto* origin:{"","http://127.0.0.1:45556","null","https://lume.local.evil.test",
                           "https://lume.local/","http://127.0.0.1:45555/","http://evil.test",
                           "https://lume.local https://evil.test"}) {
        for(const bool head:{false,true})for(const auto* range:{"","bytes=1-3"}) {
            const auto response=Request(url,range,head,origin);
            Expect(response.find(*range?"206 Partial Content":"200 OK")!=std::string::npos&&
                response.find("Access-Control-Allow-Origin:")==std::string::npos,
                "missing or disallowed origin preserves direct GET/HEAD/range without CORS");
        }
    }
    const auto duplicate=Request(url,{},false,std::string(allowed)+"\r\nOrigin: "+allowed);
    Expect(duplicate.find("400 Bad Request")!=std::string::npos&&
        duplicate.find("Access-Control-Allow-Origin:")==std::string::npos,"duplicate origin rejected without CORS");
    WallpaperMediaServer defaultServer;
    const auto defaultUrl=defaultServer.Register("local-default-cors",source);
    const auto noCors=Request(defaultUrl,{},false,allowed);
    Expect(noCors.find("200 OK")!=std::string::npos&&noCors.find("Access-Control-Allow-Origin:")==std::string::npos,
        "default wallpaper server never enables CORS even for trusted Origin");
    server.Remove("local-cors");
    }
}
}
int main() {
    WSADATA winsock{};Expect(WSAStartup(MAKEWORD(2,2),&winsock)==0,"winsock test setup");
    lume::platform::windows::WallpaperMediaServer server;
    const auto media=std::make_shared<std::string>("0123456789abcdefghijklmnopqrstuvwxyz");
    lume::platform::windows::WallpaperMediaSource source;
    source.bytes=media->size();source.contentType="video/mp4";source.extension="mp4";
    source.read=[media](std::uint64_t offset,char* output,std::size_t capacity,std::size_t& read){
        if(offset>media->size())return false;
        read=std::min(capacity,media->size()-static_cast<std::size_t>(offset));
        std::memcpy(output,media->data()+offset,read);return true;
    };
    Expect(server.Origin().empty(),"no socket or server before first media lease");
    const auto url=server.Register("local-test",source);
    Expect(url.find("http://127.0.0.1:")==0,"opaque local lease URL");
    const auto partial=Request(url,"bytes=2-5");
    Expect(partial.find("206 Partial Content")!=std::string::npos&&partial.find("Content-Range: bytes 2-5/36")!=std::string::npos&&
        partial.substr(partial.find("\r\n\r\n")+4)=="2345","exact range response");
    const auto suffix=Request(url,"bytes=-3");
    Expect(suffix.substr(suffix.find("\r\n\r\n")+4)=="xyz","suffix range");
    const auto head=Request(url,{},true);
    Expect(head.find("Content-Length: 36")!=std::string::npos&&head.substr(head.find("\r\n\r\n")+4).empty(),"HEAD does not read media body");
    Expect(Request(url,"bytes=99-").find("416 Range Not Satisfiable")!=std::string::npos,"out-of-range rejected");
    Expect(Request(url+"?token=bad").find("404 Not Found")!=std::string::npos,"exact opaque path only");
    Expect(Request(server.Origin()+"/../preferences.json").find("404 Not Found")!=std::string::npos,"no directory or path access");
    server.Remove("local-test");
    Expect(server.Origin().empty(),"last lease releases listener and server thread");
    server.Stop();server.Stop();
    source.contentType="image/gif";source.extension="gif";
    const auto gifUrl=server.Register("local-gif",source);
    Expect(!gifUrl.empty()&&Request(gifUrl).find("Content-Type: image/gif")!=std::string::npos,
        "protected GIF keeps its image MIME type");
    server.Remove("local-gif");
    TestLeaseRevocation();
    TestStopDuringClientTraffic();
    TestExactOriginCors();
    WSACleanup();
    return failures==0?0:1;
}
