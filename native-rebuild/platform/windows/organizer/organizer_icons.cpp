#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_icon_client.h"
#include <windows.h>
#include <algorithm>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace lume::platform::windows {
namespace {
std::atomic_uint workers{0};
bool Local(const std::wstring& path){
    return path.size()>=3&&path.size()<=32767&&
        ((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&
        path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/')&&path.find(L'\0')==std::wstring::npos;
}
}
unsigned OrganizerIconPixelsForDpi(unsigned dpi){
    // Grid icons are 44 DIP (list/collapsed icons are smaller). Keep the
    // original 64px floor and request real pixels for higher-DPI displays.
    return static_cast<unsigned>(std::clamp<std::uint64_t>(
        (static_cast<std::uint64_t>(dpi)*44+95)/96,64,256));
}
struct OrganizerIconCache::Impl {
    struct CachedIcon {
        std::shared_ptr<const WidgetBitmap> pixels;
        unsigned requestedSize=0;
        ULONGLONG retryAfter=0;
        std::uint64_t used=0;
    };
    explicit Impl(Loader loader,bool keepRecent):loader(std::move(loader)),keepRecent(keepRecent){}
    ~Impl(){Stop();}
    // Called under mutex. Active paths never lose their pixels to another tab.
    void Trim(){
        while(cache.size()>MaxIconPaths){
            auto oldest=cache.end();
            for(auto it=cache.begin();it!=cache.end();++it)
                if(!wanted.count(it->first)&&(oldest==cache.end()||it->second.used<oldest->second.used))oldest=it;
            if(oldest==cache.end())break;
            attempts.erase(oldest->first);cache.erase(oldest);
        }
    }
    bool SetPaths(const std::vector<std::wstring>& paths,Notify nextNotify,unsigned pixelSize){
        if(!pixelSize||pixelSize>256)return false;
        std::set<std::wstring> nextWanted;
        for(const auto& path:paths){
            if(!Local(path))return false;
            nextWanted.insert(path);
        }
        // Count unique paths: the Dock often supplies the same executable via
        // pinned, running and launchpad entries. Validate before touching the
        // live cache so a rejected update keeps the previous icons.
        if(nextWanted.size()>MaxIconPaths)return false;
        if(nextWanted.empty()&&!keepRecent){Stop();return true;}
        bool launch=false,reap=false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            wanted=std::move(nextWanted);notify=std::move(nextNotify);
            if(pixelSize!=requestedSize){requestedSize=pixelSize;attempts.clear();}
            for(auto it=cache.begin();it!=cache.end();){
                if(!keepRecent&&!wanted.count(it->first))it=cache.erase(it);
                else{if(wanted.count(it->first))it->second.used=++useSerial;++it;}
            }
            for(auto it=attempts.begin();it!=attempts.end();){
                if(!wanted.count(it->first)&&(!keepRecent||!cache.count(it->first)))it=attempts.erase(it);else ++it;
            }
            queue.clear();
            for(const auto& path:paths){
                const auto found=cache.find(path);
                if((found==cache.end()||found->second.requestedSize!=requestedSize)&&
                   !(path==loading&&loadingSize==requestedSize)&&
                   std::find(queue.begin(),queue.end(),path)==queue.end())queue.push_back(path);
            }
            // The worker changes accepting under this same lock before ending
            // an empty batch. No enqueue can fall between that check and exit.
            reap=worker.joinable()&&!accepting;
            launch=!queue.empty()&&!accepting;
            if(launch)accepting=true;
        }
        if(reap)worker.join();
        if(launch){
            cancelled=false;
            std::promise<bool> ready;auto initialized=ready.get_future();
            try{
                worker=std::thread([this,ready=std::move(ready)]()mutable{Run(std::move(ready));});
            }catch(...){Stop();return false;}
            if(!initialized.get()){Stop();return false;}
        }
        return true;
    }
    void Run(std::promise<bool> ready){
        OrganizerIconExtractor extractor;
        ++workers;ready.set_value(true);
        for(;;){
            std::wstring path;unsigned pixelSize=0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(cancelled||queue.empty()){accepting=false;break;}
                path=std::move(queue.front());queue.pop_front();loading=path;
                pixelSize=requestedSize;loadingSize=pixelSize;
                ++attempts[path];
            }
            std::shared_ptr<const WidgetBitmap> pixels;
            try{pixels=loader?loader(path,cancelled,pixelSize):extractor.Read(path,cancelled,pixelSize);}catch(...){}
            // Keep the bound even for an injected or faulty decoder.
            if(pixels&&(!pixels->width||!pixels->height||pixels->width>256||pixels->height>256||
                pixels->pixels.size()!=static_cast<std::size_t>(pixels->width)*pixels->height))pixels.reset();
            Notify callback;
            {
                std::lock_guard<std::mutex> lock(mutex);
                loading.clear();loadingSize=0;
                if(cancelled){accepting=false;break;}
                const bool active=wanted.count(path)!=0;
                if((active||keepRecent)&&pixelSize==requestedSize){
                    // A cold Shell lookup may fail once. Retry only inside this
                    // finite batch; a permanent miss must not retain a worker
                    // or restart extraction on every repaint.
                    if(!pixels&&active&&attempts[path]%2==1&&attempts[path]<6)queue.push_back(path);
                    else{
                        auto& entry=cache[path];
                        if(pixels)entry.pixels=std::move(pixels);
                        entry.used=++useSerial;
                        entry.retryAfter=GetTickCount64()+(attempts[path]<=2?5000:30000);
                        entry.requestedSize=pixelSize;if(active)callback=notify;
                        Trim();
                    }
                }
            }
            if(callback){try{callback();}catch(...){}}
        }
        extractor.Stop();--workers;
    }
    bool RetryMissing(){
        std::vector<std::wstring> paths;Notify callback;unsigned size=0;bool retry=false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for(auto& [path,entry]:cache)
                if(wanted.count(path)&&!entry.pixels&&entry.requestedSize==requestedSize&&attempts[path]<6&&GetTickCount64()>=entry.retryAfter){
                    entry.requestedSize=0;retry=true;
                }
            if(!retry)return true;
            paths.assign(wanted.begin(),wanted.end());callback=notify;size=requestedSize;
        }
        return SetPaths(paths,std::move(callback),size);
    }
    void Stop(){
        cancelled=true;
        if(worker.joinable())worker.join();
        std::lock_guard<std::mutex> lock(mutex);
        cache.clear();wanted.clear();queue.clear();attempts.clear();loading.clear();loadingSize=0;notify={};accepting=false;
    }
    std::shared_ptr<const WidgetBitmap> Find(const std::wstring& path)const{
        std::lock_guard<std::mutex> lock(mutex);
        const auto it=cache.find(path);return it==cache.end()?nullptr:it->second.pixels;
    }
    Loader loader;const bool keepRecent;Notify notify;std::thread worker;
    mutable std::mutex mutex;bool accepting=false;
    std::atomic_bool cancelled{false};
    std::set<std::wstring> wanted;
    std::map<std::wstring,CachedIcon> cache;
    std::map<std::wstring,unsigned> attempts;
    std::deque<std::wstring> queue;std::wstring loading;
    unsigned requestedSize=64,loadingSize=0;
    std::uint64_t useSerial=0;
};
OrganizerIconCache::OrganizerIconCache(Loader loader,bool keepRecent):impl_(std::make_unique<Impl>(std::move(loader),keepRecent)){}
OrganizerIconCache::~OrganizerIconCache()=default;
bool OrganizerIconCache::SetPaths(const std::vector<std::wstring>& paths,Notify notify,unsigned pixelSize){return impl_->SetPaths(paths,std::move(notify),pixelSize);}
bool OrganizerIconCache::RetryMissing(){return impl_->RetryMissing();}
std::shared_ptr<const WidgetBitmap> OrganizerIconCache::Find(const std::wstring& path)const{return impl_->Find(path);}
std::size_t OrganizerIconCache::CachedPaths()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->cache.size();}
void OrganizerIconCache::Stop(){impl_->Stop();}
unsigned OrganizerIconCache::ActiveWorkers(){return workers.load();}
}
