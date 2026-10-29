#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include "platform/windows/wallpaper/media_server.h"
#include "platform/windows/wallpaper/wallpaper_pipe.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include <algorithm>
#include <cstring>
#include <array>
#include <atomic>
#include <charconv>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lume::platform::windows {
namespace {
bool SendAll(SOCKET socket,const char* data,std::size_t count) {
    while(count) {
        const int sent=send(socket,data,static_cast<int>(std::min<std::size_t>(count,65536)),0);
        if(sent<=0)return false;
        data+=sent;count-=static_cast<std::size_t>(sent);
    }
    return true;
}
bool Number(std::string_view text,std::uint64_t& value) {
    if(text.empty()||text.size()>20)return false;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size();
}
std::string Token() {
    std::array<UCHAR,32> bytes{};
    if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
    constexpr char digits[]="0123456789abcdef";std::string token;
    for(const auto byte:bytes){token.push_back(digits[byte>>4]);token.push_back(digits[byte&15]);}
    return token;
}
std::string Lower(std::string value) {
    for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return value;
}
bool ValidOrigin(std::string_view origin) {
    if(origin=="https://lume.local")return true;
    // Explicit loopback origins are for isolated browser fixtures only.
    constexpr std::string_view prefix="http://127.0.0.1:";
    if(origin.substr(0,prefix.size())!=prefix)return false;
    const auto port=origin.substr(prefix.size());
    std::uint64_t value=0;
    return !port.empty()&&port.front()!='0'&&Number(port,value)&&value<=65535;
}
struct PlaintextBuffer {
    std::array<char,65536> bytes{};
    ~PlaintextBuffer(){SecureZeroMemory(bytes.data(),bytes.size());}
};
}
struct WallpaperMediaServer::Impl {
    struct Entry {
        std::string id;
        WallpaperMediaSource source;
        std::atomic<bool> revoked{false};
        Entry(std::string id,WallpaperMediaSource source):id(std::move(id)),source(std::move(source)){}
    };
    struct Client {
        // Only the serving thread closes a client socket. Shutdown and Close
        // share this lock so cancellation can never touch a recycled SOCKET.
        std::mutex socketMutex;
        SOCKET socket=INVALID_SOCKET;
        std::string leaseId; // guarded by the server mutex
        std::atomic<bool> done{false};
        std::thread thread;
        ~Client(){Close();}
        SOCKET GetSocket(){std::lock_guard<std::mutex> lock(socketMutex);return socket;}
        void Shutdown(){
            std::lock_guard<std::mutex> lock(socketMutex);
            if(socket!=INVALID_SOCKET)shutdown(socket,SD_BOTH);
        }
        void Close(){
            std::lock_guard<std::mutex> lock(socketMutex);
            if(socket!=INVALID_SOCKET){shutdown(socket,SD_BOTH);closesocket(socket);socket=INVALID_SOCKET;}
        }
    };
    // The wallpaper player reads the same leases over a named pipe (see
    // wallpaper_pipe.h). Security software that filters loopback TCP for
    // unsigned programs leaves pipes alone. Overlapped I/O lets Revoke/Stop
    // interrupt a blocked read or write.
    struct PipeClient {
        HANDLE pipe=INVALID_HANDLE_VALUE;
        HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::string leaseId; // guarded by the server mutex
        std::atomic<bool> done{false};
        std::thread thread;
        ~PipeClient(){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);if(cancel)CloseHandle(cancel);}
        void Shutdown(){if(cancel)SetEvent(cancel);}
        bool Io(bool write,void* data,DWORD size) {
            auto* bytes=static_cast<char*>(data);
            while(size) {
                OVERLAPPED overlapped{};overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                if(!overlapped.hEvent)return false;
                DWORD moved=0;
                BOOL ok=write?WriteFile(pipe,bytes,size,nullptr,&overlapped):ReadFile(pipe,bytes,size,nullptr,&overlapped);
                if(!ok&&GetLastError()==ERROR_IO_PENDING) {
                    HANDLE waits[]={overlapped.hEvent,cancel};
                    if(WaitForMultipleObjects(2,waits,FALSE,30000)!=WAIT_OBJECT_0){
                        CancelIoEx(pipe,&overlapped);GetOverlappedResult(pipe,&overlapped,&moved,TRUE);
                        CloseHandle(overlapped.hEvent);return false;
                    }
                    ok=TRUE;
                }
                ok=ok&&GetOverlappedResult(pipe,&overlapped,&moved,TRUE);
                CloseHandle(overlapped.hEvent);
                if(!ok||!moved)return false;
                bytes+=moved;size-=moved;
            }
            return true;
        }
    };
    std::vector<std::unique_ptr<PipeClient>> pipeClients;
    HANDLE pipeStop=nullptr;
    std::thread pipeAcceptor;
    std::wstring pipeName;
    std::shared_ptr<Entry> Find(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto entry=entries.find(path);
        return entry==entries.end()?nullptr:entry->second;
    }
    void ServePipe(PipeClient& client) {
        char magic[4]{};std::uint32_t length=0;
        if(!client.Io(false,magic,4)||std::memcmp(magic,wallpaper_pipe::Magic,4)!=0||
           !client.Io(false,&length,4)||!length||length>wallpaper_pipe::MaxPath)return;
        std::string path(length,'\0');
        if(!client.Io(false,path.data(),length))return;
        const auto lease=Find(path);
        std::uint64_t size=lease&&!lease->revoked&&lease->source.read?lease->source.bytes:0;
        if(lease){std::lock_guard<std::mutex> lock(mutex);client.leaseId=lease->id;}
        if(!client.Io(true,&size,8)||!size)return;
        PlaintextBuffer buffer;
        while(!stopping&&!lease->revoked) {
            wallpaper_pipe::Request request{};
            if(!client.Io(false,&request,sizeof(request)))return;
            std::uint32_t count=0;
            if(request.offset<size) {
                const auto capacity=static_cast<std::size_t>(std::min<std::uint64_t>(
                    {buffer.bytes.size(),request.length,size-request.offset}));
                std::size_t read=0;
                if(capacity&&lease->source.read(request.offset,buffer.bytes.data(),capacity,read)&&read<=capacity)
                    count=static_cast<std::uint32_t>(read);
            }
            if(lease->revoked||!client.Io(true,&count,4)||(count&&!client.Io(true,buffer.bytes.data(),count)))return;
        }
    }
    void AcceptPipes() {
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,FALSE};
        // Only this user (and the system) may connect.
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)(A;;GA;;;SY)",SDDL_REVISION_1,
            &security.lpSecurityDescriptor,nullptr);
        while(!stopping) {
            HANDLE pipe=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
                PIPE_UNLIMITED_INSTANCES,1<<20,4096,0,security.lpSecurityDescriptor?&security:nullptr);
            if(pipe==INVALID_HANDLE_VALUE){if(WaitForSingleObject(pipeStop,200)==WAIT_OBJECT_0)break;continue;}
            OVERLAPPED overlapped{};overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            bool connected=ConnectNamedPipe(pipe,&overlapped)!=FALSE||GetLastError()==ERROR_PIPE_CONNECTED;
            if(!connected&&GetLastError()==ERROR_IO_PENDING) {
                HANDLE waits[]={overlapped.hEvent,pipeStop};
                DWORD ignored=0;
                if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0)connected=GetOverlappedResult(pipe,&overlapped,&ignored,FALSE)!=FALSE;
                else {CancelIoEx(pipe,&overlapped);GetOverlappedResult(pipe,&overlapped,&ignored,TRUE);}
            }
            if(overlapped.hEvent)CloseHandle(overlapped.hEvent);
            if(!connected||stopping){CloseHandle(pipe);continue;}
            std::lock_guard<std::mutex> lock(mutex);
            for(auto it=pipeClients.begin();it!=pipeClients.end();) {
                if((*it)->done){if((*it)->thread.joinable())(*it)->thread.join();it=pipeClients.erase(it);}
                else ++it;
            }
            if(pipeClients.size()>=16||stopping){CloseHandle(pipe);continue;}
            auto created=std::make_unique<PipeClient>();created->pipe=pipe;
            auto* client=created.get();
            pipeClients.push_back(std::move(created));
            try {
                client->thread=std::thread([this,client]{
                    try{ServePipe(*client);}catch(...){}
                    FlushFileBuffers(client->pipe);DisconnectNamedPipe(client->pipe);
                    {std::lock_guard<std::mutex> lock(mutex);client->leaseId.clear();}
                    client->done=true;
                });
            }catch(...){pipeClients.pop_back();}
        }
        if(security.lpSecurityDescriptor)LocalFree(security.lpSecurityDescriptor);
    }
    // Public lifecycle changes may join workers, so they must never hold mutex
    // while waiting. This separate lock serializes Register/Remove/Stop.
    std::mutex lifecycleMutex;
    mutable std::mutex mutex;
    std::map<std::string,std::shared_ptr<Entry>> entries;
    std::vector<std::unique_ptr<Client>> clients;
    std::atomic<SOCKET> listener{INVALID_SOCKET};
    std::atomic<bool> stopping{false};
    std::thread acceptor;
    std::string origin;
    std::string allowedOrigin;
    bool winsock=false;

    void Reply(SOCKET socket,const std::string& status,const std::string& headers={}) {
        const auto response="HTTP/1.1 "+status+"\r\nConnection: close\r\nCache-Control: no-store\r\n"+
            headers+"Content-Length: 0\r\n\r\n";
        SendAll(socket,response.data(),response.size());
    }
    void Serve(Client& client) {
        const auto socket=client.GetSocket();
        std::string request;request.reserve(4096);char bytes[1024];
        while(request.find("\r\n\r\n")==std::string::npos&&request.size()<=8192&&!stopping) {
            const int count=recv(socket,bytes,sizeof(bytes),0);
            if(count<=0)return;
            request.append(bytes,count);
        }
        if(stopping)return;
        if(request.size()>8192){Reply(socket,"431 Request Header Fields Too Large");return;}
        const auto first=request.find("\r\n"),space=request.find(' '),last=request.find(' ',space+1);
        if(first==std::string::npos||space==std::string::npos||last==std::string::npos||last>first) {
            Reply(socket,"400 Bad Request");return;
        }
        const auto method=request.substr(0,space),path=request.substr(space+1,last-space-1);
        if(method!="GET"&&method!="HEAD"){Reply(socket,"405 Method Not Allowed","Allow: GET, HEAD\r\n");return;}
        std::string requestOrigin;bool hasOrigin=false;
        std::shared_ptr<Entry> lease;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto entry=entries.find(path);
            if(entry!=entries.end()){
                lease=entry->second;
                client.leaseId=lease->id;
            }
        }
        if(!lease||lease->revoked||!lease->source.read){Reply(socket,"404 Not Found");return;}
        const auto& source=lease->source;
        std::string range;bool hasRange=false;
        auto position=first+2;
        while(position<request.size()) {
            const auto end=request.find("\r\n",position);
            if(end==std::string::npos||end==position)break;
            const auto line=request.substr(position,end-position);
            const auto separator=line.find(':');
            if(separator==std::string::npos){Reply(socket,"400 Bad Request");return;}
            if(Lower(line.substr(0,separator))=="range") {
                if(hasRange){Reply(socket,"400 Bad Request");return;}
                hasRange=true;range=line.substr(separator+1);
                while(!range.empty()&&(range.front()==' '||range.front()=='\t'))range.erase(0,1);
            } else if(!allowedOrigin.empty()&&Lower(line.substr(0,separator))=="origin") {
                if(hasOrigin){Reply(socket,"400 Bad Request");return;}
                hasOrigin=true;requestOrigin=line.substr(separator+1);
                while(!requestOrigin.empty()&&(requestOrigin.front()==' '||requestOrigin.front()=='\t'))requestOrigin.erase(0,1);
                while(!requestOrigin.empty()&&(requestOrigin.back()==' '||requestOrigin.back()=='\t'))requestOrigin.pop_back();
                if(requestOrigin.size()>256){Reply(socket,"400 Bad Request");return;}
            }
            position=end+2;
        }
        const std::string cors=(hasOrigin&&!allowedOrigin.empty()&&requestOrigin==allowedOrigin)
            ?"Access-Control-Allow-Origin: "+allowedOrigin+"\r\nVary: Origin\r\n":"";
        std::uint64_t begin=0,end=source.bytes-1;
        if(hasRange) {
            bool valid=range.compare(0,6,"bytes=")==0&&range.find(',')==std::string::npos;
            const auto dash=range.find('-',6);
            if(!valid||dash==std::string::npos)valid=false;
            else {
                const auto left=range.substr(6,dash-6),right=range.substr(dash+1);
                if(left.empty()) {
                    std::uint64_t suffix=0;valid=Number(right,suffix)&&suffix>0;
                    if(valid)begin=source.bytes-std::min(suffix,source.bytes);
                }else {
                    valid=Number(left,begin)&&begin<source.bytes;
                    if(!right.empty())valid=valid&&Number(right,end)&&end>=begin;
                    end=std::min(end,source.bytes-1);
                }
            }
            if(!valid){Reply(socket,"416 Range Not Satisfiable",cors+"Content-Range: bytes */"+std::to_string(source.bytes)+"\r\n");return;}
        }
        const auto length=end-begin+1;
        const auto headers="HTTP/1.1 "+std::string(hasRange?"206 Partial Content":"200 OK")+
            "\r\nConnection: close\r\nCache-Control: no-store\r\nAccept-Ranges: bytes\r\nContent-Type: "+
            source.contentType+"\r\nContent-Length: "+std::to_string(length)+"\r\n"+
            cors+
            (hasRange?"Content-Range: bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/"+std::to_string(source.bytes)+"\r\n":"")+
            "\r\n";
        if(lease->revoked||!SendAll(socket,headers.data(),headers.size())||method=="HEAD")return;
        PlaintextBuffer buffer;
        for(auto offset=begin;offset<=end&&!stopping&&!lease->revoked;) {
            const auto capacity=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.bytes.size(),end-offset+1));
            std::size_t read=0;
            if(!source.read(offset,buffer.bytes.data(),capacity,read)||read==0||read>capacity||
                lease->revoked||stopping||!SendAll(socket,buffer.bytes.data(),read))return;
            offset+=read;
        }
    }
    void Accept() {
        while(!stopping) {
            const SOCKET current=listener.load();
            if(current==INVALID_SOCKET)break;
            fd_set ready;FD_ZERO(&ready);FD_SET(current,&ready);timeval timeout{0,200000};
            if(select(0,&ready,nullptr,nullptr,&timeout)<=0)continue;
            sockaddr_in address{};int size=sizeof(address);
            const SOCKET socket=accept(current,reinterpret_cast<sockaddr*>(&address),&size);
            if(socket==INVALID_SOCKET)continue;
            if(address.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){closesocket(socket);continue;}
            u_long blocking=0;
            if(ioctlsocket(socket,FIONBIO,&blocking)!=0){closesocket(socket);continue;}
            DWORD millis=2000;
            setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&millis),sizeof(millis));
            setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&millis),sizeof(millis));
            std::lock_guard<std::mutex> lock(mutex);
            for(auto it=clients.begin();it!=clients.end();) {
                if((*it)->done){if((*it)->thread.joinable())(*it)->thread.join();it=clients.erase(it);}
                else ++it;
            }
            if(clients.size()>=16||stopping){closesocket(socket);continue;}
            auto created=std::make_unique<Client>();created->socket=socket;
            auto* client=created.get();
            clients.push_back(std::move(created));
            try {
                client->thread=std::thread([this,client]{
                    try{Serve(*client);}catch(...){}
                    client->Close();
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        client->leaseId.clear();
                    }
                    client->done=true;
                });
            }catch(...){client->Close();clients.pop_back();}
        }
    }
    bool Start() {
        if(listener!=INVALID_SOCKET)return true;
        WSADATA data{};
        if(WSAStartup(MAKEWORD(2,2),&data)!=0)return false;
        winsock=true;stopping=false;
        const SOCKET socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(socket==INVALID_SOCKET){WSACleanup();winsock=false;return false;}
        BOOL exclusive=TRUE;setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        int size=sizeof(address);
        u_long nonblocking=1;
        if(bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||listen(socket,16)!=0||
            ioctlsocket(socket,FIONBIO,&nonblocking)!=0||
            getsockname(socket,reinterpret_cast<sockaddr*>(&address),&size)!=0) {
            closesocket(socket);WSACleanup();winsock=false;return false;
        }
        listener=socket;origin="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port));
        try{acceptor=std::thread([this]{Accept();});}
        catch(...){listener=INVALID_SOCKET;closesocket(socket);origin.clear();WSACleanup();winsock=false;return false;}
        // The pipe is named after the port this process holds exclusively.
        pipeName=wallpaper_pipe::Name(ntohs(address.sin_port));
        pipeStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(pipeStop){try{pipeAcceptor=std::thread([this]{AcceptPipes();});}catch(...){}}
        return true;
    }
    void Stop() {
        stopping=true;
        const auto socket=listener.load();
        // Do not close before the acceptor stops using its local SOCKET copy.
        if(socket!=INVALID_SOCKET)shutdown(socket,SD_BOTH);
        if(acceptor.joinable())acceptor.join();
        listener=INVALID_SOCKET;
        if(socket!=INVALID_SOCKET)closesocket(socket);
        if(pipeStop)SetEvent(pipeStop);
        if(pipeAcceptor.joinable())pipeAcceptor.join();
        if(pipeStop){CloseHandle(pipeStop);pipeStop=nullptr;}
        std::vector<std::unique_ptr<Client>> owned;
        std::vector<std::unique_ptr<PipeClient>> ownedPipes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for(const auto& item:entries)item.second->revoked=true;
            owned=std::move(clients);ownedPipes=std::move(pipeClients);entries.clear();origin.clear();
        }
        for(const auto& client:owned)client->Shutdown();
        for(const auto& client:ownedPipes)client->Shutdown();
        for(const auto& client:owned)if(client->thread.joinable())client->thread.join();
        for(const auto& client:ownedPipes)if(client->thread.joinable())client->thread.join();
        if(winsock){WSACleanup();winsock=false;}
    }
    void Revoke(const std::string& id) {
        std::vector<std::unique_ptr<Client>> revokedClients;
        std::vector<std::unique_ptr<PipeClient>> revokedPipes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for(auto it=entries.begin();it!=entries.end();) {
                if(it->second->id==id){it->second->revoked=true;it=entries.erase(it);}
                else ++it;
            }
            for(auto it=pipeClients.begin();it!=pipeClients.end();) {
                if((*it)->leaseId==id){revokedPipes.push_back(std::move(*it));it=pipeClients.erase(it);}
                else ++it;
            }
            for(auto it=clients.begin();it!=clients.end();) {
                if((*it)->leaseId==id){
                    revokedClients.push_back(std::move(*it));
                    it=clients.erase(it);
                }else ++it;
            }
        }
        for(const auto& client:revokedClients)client->Shutdown();
        // A bounded socket operation is interrupted before joining. Joining
        // ensures the revoked reader and its key are gone when Remove returns.
        for(const auto& client:revokedClients)if(client->thread.joinable())client->thread.join();
        for(const auto& client:revokedPipes)client->Shutdown();
        for(const auto& client:revokedPipes)if(client->thread.joinable())client->thread.join();
    }
};
WallpaperMediaServer::WallpaperMediaServer(std::string allowedOrigin):impl_(std::make_unique<Impl>()) {
    if(!allowedOrigin.empty()&&!ValidOrigin(allowedOrigin))throw std::invalid_argument("invalid media server origin");
    impl_->allowedOrigin=std::move(allowedOrigin);
}
WallpaperMediaServer::~WallpaperMediaServer(){Stop();}
std::string WallpaperMediaServer::Register(const std::string& id,WallpaperMediaSource source) {
    if(!lume::wallpaper::IsSafeWallpaperId(id)||!source.bytes||source.bytes>1073741824ULL||!source.read||
        (source.contentType!="video/mp4"&&source.contentType!="video/webm"&&source.contentType!="video/x-matroska"&&
         source.contentType!="image/jpeg"&&source.contentType!="image/png"&&source.contentType!="image/webp"&&
         source.contentType!="image/bmp"&&source.contentType!="image/gif"&&source.contentType!="application/octet-stream")||
        source.extension.empty()||source.extension.size()>8||
        !std::all_of(source.extension.begin(),source.extension.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');}))return {};
    const auto token=Token();if(token.empty())return {};
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    impl_->Revoke(id);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(impl_->entries.size()>=128)return {};
    if(!impl_->Start())return {};
    const auto path="/"+token+"/media."+source.extension;
    impl_->entries.emplace(path,std::make_shared<Impl::Entry>(id,std::move(source)));
    return impl_->origin+path;
}
void WallpaperMediaServer::Remove(const std::string& id) {
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    impl_->Revoke(id);
    bool empty=false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        empty=impl_->entries.empty();
    }
    if(empty)impl_->Stop();
}
void WallpaperMediaServer::Stop(){
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycleMutex);
    impl_->Stop();
}
std::string WallpaperMediaServer::Origin()const {
    std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->origin;
}
unsigned WallpaperMediaServer::Clients()const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<unsigned>(std::count_if(impl_->clients.begin(),impl_->clients.end(),[](const auto& c){return !c->done;})+
        std::count_if(impl_->pipeClients.begin(),impl_->pipeClients.end(),[](const auto& c){return !c->done;}));
}
}
