#include "apps/shell/music_font_asset_session.h"
#include "platform/windows/webview_page_window.h"
#include "platform/windows/wallpaper/media_server.h"
#include "unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
lume::platform::windows::WebViewPageWindow* active = nullptr;
bool timedOut = false;
void Require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
std::string Read(const fs::path& path) {
    const auto size = fs::file_size(path);
    Require(size > 0 && size <= 50ULL * 1024 * 1024, "fixture size limit");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(input.good(), "fixture read");
    return bytes;
}
Json Message(lume::shell::MusicFontAssetSession& session, std::wstring_view command) {
    const auto reply = session.HandleMessage(command);
    Require(reply.has_value(), "font command handled");
    return Json::parse(reply->begin(), reply->end());
}
void CALLBACK Deadline(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    timedOut = true;
    if (active) active->RequestClose();
}
bool Run(const fs::path& root) {
    // A complete 1x1 red PNG, served by an independent music-owned lease.
    static constexpr unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };
    lume::platform::windows::WallpaperMediaServer mediaServer("https://lume.local");
    lume::platform::windows::WallpaperMediaSource media;
    media.bytes = sizeof(png); media.contentType = "image/png"; media.extension = "png";
    media.read = [](std::uint64_t offset, char* output, std::size_t capacity, std::size_t& read) {
        read = 0;
        if (offset > sizeof(png)) return false;
        read = std::min(capacity, sizeof(png) - static_cast<std::size_t>(offset));
        std::memcpy(output, png + offset, read);
        return true;
    };
    const auto imageUrl = mediaServer.Register("local-music-resource-fixture", std::move(media));
    Require(!imageUrl.empty(), "isolated image lease registration");
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    Require(length > 0 && length < MAX_PATH, "Windows directory lookup");
    fs::path systemFont;
    for (const auto* name : {L"arial.ttf", L"segoeui.ttf", L"tahoma.ttf"}) {
        const auto path = fs::path(windows) / "Fonts" / name;
        if (fs::is_regular_file(path)) { systemFont = path; break; }
    }
    Require(!systemFont.empty(), "real installed TrueType font required");
    const auto sourceWriteTime = fs::last_write_time(systemFont);
    const auto bytes = Read(systemFont);
    const auto source = root / "SystemFont.ttf";
    {
        // Installed fonts can be WinSxS hardlinks; the isolated copy preserves
        // the importer's strict source checks without changing system files.
        std::ofstream output(source, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(output.good(), "isolated real font copy");
    }
    lume::shell::MusicFontAssetSessionOptions options;
    options.root = root / "owned";
    options.verification = true;
    int picks = 0;
    options.chooseFont = [&](HWND) { ++picks; return source; };
    Json asset;
    {
        lume::shell::MusicFontAssetSession importing(options);
        const auto reply = Message(importing,
            LR"({"version":1,"scope":"music-font-assets","action":"choose","request":1})");
        Require(reply.at("error") == "" && reply.at("candidate").is_object() && picks == 1,
            "native real font import");
        asset = reply.at("candidate");
        Require(asset.size() == 5 && asset.at("extension") == "ttf" &&
            asset.at("label") == "SystemFont" && asset.at("size") == bytes.size() &&
            asset.at("family") == "LumeImportedFont_" + asset.at("id").get<std::string>() &&
            reply.at("assets") == Json::array({asset}), "metadata-only real font candidate");
    }
    const auto manifest = Read(options.root / "font-assets.json");
    Require(Json::parse(manifest).at("assets") == Json::array({asset}), "persisted font metadata");
    fs::remove(source);
    lume::shell::MusicFontAssetSession restored(options);
    const auto snapshot = Message(restored,
        LR"({"version":1,"scope":"music-font-assets","action":"snapshot"})");
    Require(snapshot.at("error") == "" && snapshot.at("assets") == Json::array({asset}) &&
        snapshot.at("candidate").is_null() && picks == 1, "new session restores without source or chooser");
    const auto font = restored.AssetDirectory() / (asset.at("id").get<std::string>() + ".ttf");
    Require(Read(font) == bytes, "restored immutable file matches system font bytes");
    const auto unregistered = restored.AssetDirectory() / "00000000000000000000000000000000.ttf";
    Require(unregistered != font, "distinct unregistered font fixture");
    fs::copy_file(font, unregistered);
    const auto extraLink = root / "font-read-hardlink";

    lume::platform::windows::WebViewPageWindow window;
    active = &window;
    int fontLookups = 0;
    window.ConfigureMusicFonts(restored.AssetDirectory().wstring(), [&](std::wstring_view requested) {
        ++fontLookups;
        return restored.AssetSize(requested);
    });
    bool complete = false, passed = false;
    int snapshots = 0, linkSteps = 0;
    std::string phase = "startup";
    window.ConfigureApplicationCommands([&](std::wstring_view message) {
        const auto json = Json::parse(message.begin(), message.end(), nullptr, false);
        if (!json.is_object()) return;
        if (json.value("scope", "") == "music-font-assets") {
            if (const auto reply = restored.HandleMessage(message)) {
                ++snapshots;
                window.Post(*reply);
            }
            return;
        }
        if (json.value("scope", "") != "resource-fixture") return;
        const auto action = json.value("action", "");
        if (action == "resize") {
            const int width = json.value("width", 960), height = json.value("height", 540);
            SetWindowPos(window.Handle(), nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        if (action == "link" || action == "unlink") {
            const bool ok = action == "link" ?
                CreateHardLinkW(extraLink.c_str(), font.c_str(), nullptr) != FALSE :
                DeleteFileW(extraLink.c_str()) != FALSE;
            ++linkSteps;
            const auto response = Json{{"type", "resource-fixture-step"},
                {"request", json.at("request")}, {"ok", ok}}.dump();
            window.Post(std::wstring(response.begin(), response.end()));
            return;
        }
        complete = true;
        phase = json.value("phase", "");
        passed = json.value("metadata", false) && json.value("image", false) &&
            json.value("font", false) && json.value("iframe", false) &&
            json.value("guard", false) && json.value("hardlink", false) && json.value("lumiere", false);
        window.RequestClose();
    });
    const auto timer = SetTimer(nullptr, 0, 25000, Deadline);
    Require(timer != 0, "WebView deadline timer");
    const auto expected = asset.dump(-1, ' ', true);
    const auto imageArgument = Json(imageUrl).dump();
    const std::wstring script = LR"JS(
      (async(expected,imageUrl)=>{
        const result={version:1,scope:'resource-fixture',metadata:false,image:false,font:false,iframe:false,guard:false,hardlink:false,phase:'metadata'};
        let face,childFace,frame,lumiere,stage;
        const step=(action,request)=>new Promise(resolve=>{
          const receive=event=>{
            if(event.data?.type!=='resource-fixture-step'||event.data.request!==request)return;
            chrome.webview.removeEventListener('message',receive);resolve(event.data.ok);
          };
          chrome.webview.addEventListener('message',receive);
          chrome.webview.postMessage({version:1,scope:'resource-fixture',action,request});
        });
        const denied=async url=>{
          try {return !(await fetch(url,{credentials:'omit',cache:'no-store'})).ok;}
          catch {return true;}
        };
        try {
          const state=await new Promise(resolve=>{
            const receive=event=>{
              if(event.data?.type!=='music-font-assets-state'||event.data.request!==0)return;
              chrome.webview.removeEventListener('message',receive);resolve(event.data);
            };
            chrome.webview.addEventListener('message',receive);
            chrome.webview.postMessage({version:1,scope:'music-font-assets',action:'snapshot'});
          });
          const asset=state.assets?.[0];
          if(state.version!==1||state.error!==''||state.candidate!==null||state.assets.length!==1||
             !asset||Object.keys(asset).length!==5||
             !Object.keys(expected).every(key=>asset[key]===expected[key])||
             !/^[0-9a-f]{32}$/.test(asset.id)||asset.family!=='LumeImportedFont_'+asset.id)throw Error('metadata');
          result.metadata=true;
          result.phase='image';
          const image=new Image();image.crossOrigin='anonymous';
          image.src=imageUrl;
          await image.decode();
          const canvas=document.createElement('canvas');canvas.width=canvas.height=8;
          const context=canvas.getContext('2d');
          context.drawImage(image,0,0,8,8);
          const pixels=context.getImageData(0,0,8,8).data;
          result.image=pixels.every((value,index)=>value===(index%4===0||index%4===3?255:0));
          const url='https://lume-fonts.local/'+asset.id+'.'+asset.extension;
          result.phase='font-guard';
          result.guard=await denied('https://lume-fonts.local/00000000000000000000000000000000.ttf')&&
            await denied(url+'?unexpected=1')&&
            await denied('https://lume-fonts.local/%2e%2e%2ffont-assets.json');
          if(!result.guard)throw Error('guard');
          result.phase='font-hardlink';
          if(!await step('link',1))throw Error('link-fixture');
          result.hardlink=await denied(url);
          if(!await step('unlink',2)||!result.hardlink)throw Error('hardlink-guard');
          result.phase='parent-font';
          const response=await fetch(url,{credentials:'omit',cache:'no-store'});
          if(!response.ok)throw Error('fetch');
          const bytes=await response.arrayBuffer();
          if(bytes.byteLength!==asset.size)throw Error('size');
          face=await new FontFace(asset.family,bytes).load();
          document.fonts.add(face);
          result.font=face.status==='loaded'&&document.fonts.has(face)&&document.fonts.check('16px '+asset.family);
          result.phase='iframe-font';
          frame=document.createElement('iframe');frame.src='about:blank';
          const ready=new Promise(resolve=>{frame.onload=resolve;});
          document.body.append(frame);await ready;
          const realm=frame.contentWindow,child=frame.contentDocument;
          const childResponse=await realm.fetch(url,{credentials:'omit',cache:'no-store'});
          if(!childResponse.ok)throw Error('iframe-fetch');
          const childBytes=await childResponse.arrayBuffer();
          const childValues=new realm.Uint8Array(childBytes);
          if(childBytes.byteLength!==asset.size||
             !new Uint8Array(bytes).every((value,index)=>value===childValues[index]))throw Error('iframe-bytes');
          childFace=await new realm.FontFace(asset.family,childBytes).load();
          child.fonts.add(childFace);
          result.iframe=childFace.status==='loaded'&&child.fonts.has(childFace)&&child.fonts.check('16px '+asset.family);
          result.phase='lumiere-production-csp';
          const {mountLumiere}=await import('./music-visualizers/lumiere.js');
          const {DEFAULT_MUSIC_PREFERENCES}=await import('./music-preference-values.js');
          stage=document.createElement('div');
          stage.style.cssText='position:fixed;inset:0';document.body.append(stage);
          let renderError;
          lumiere=mountLumiere(stage,{visible:true,playing:false,currentTime:8,duration:30,
            current:{id:'csp-test',title:'Lumiere'},
            preferences:{...DEFAULT_MUSIC_PREFERENCES,mode:'lumiere',fontFamily:asset.family},
            lines:[{startTime:0,endTime:20,fullText:'Lume light',words:[]}]},{onError:error=>{renderError=error;}});
          const deadline=performance.now()+6000;
          while(!stage.querySelector('[data-lumiere-state="ready"] canvas')&&!renderError&&performance.now()<deadline)
            await new Promise(resolve=>setTimeout(resolve,20));
          if(renderError)throw renderError;
          const rendered=stage.querySelector('[data-lumiere-state="ready"] canvas');
          result.lumiere=!!rendered&&rendered.width>1&&rendered.height>1;
          if(!result.lumiere)throw Error('lumiere-not-ready');
          for(const [width,height] of [[1600,900],[960,540],[1800,1012],[1200,675],[0,0],[0,0],[0,0],[0,0]]){
            if(width)chrome.webview.postMessage({scope:'resource-fixture',action:'resize',width,height});
            else chrome.webview.postMessage('v1|window|maximize');
            await new Promise(resolve=>setTimeout(resolve,500));
            const bounds=stage.getBoundingClientRect(),canvas=rendered.getBoundingClientRect();
            const scale=Math.min(2,Math.max(1,devicePixelRatio));
            if(Math.abs(bounds.width-canvas.width)>1||Math.abs(bounds.height-canvas.height)>1||
              Math.abs(rendered.width-bounds.width*scale)>2||Math.abs(rendered.height-bounds.height*scale)>2){
              result.lumiere=false;result.phase='resize-'+JSON.stringify({dpr:devicePixelRatio,
                host:[bounds.width,bounds.height],css:[canvas.width,canvas.height],pixels:[rendered.width,rendered.height]});
              throw Error('lumiere-resize');
            }
          }
          result.phase='complete';
        } catch {} finally {
          lumiere?.dispose();stage?.remove();
          if(childFace)frame?.contentDocument?.fonts.delete(childFace);
          frame?.remove();if(face)document.fonts.delete(face);
        }
        chrome.webview.postMessage(result);
      })()JS" + std::wstring(expected.begin(), expected.end()) + L"," +
        std::wstring(imageArgument.begin(), imageArgument.end()) + L");";
    const auto result = window.Run(GetModuleHandleW(nullptr), [](lume::ui::PageCommand) {
        return std::wstring{};
    }, true, script);
    KillTimer(nullptr, timer);active = nullptr;
    mediaServer.Remove("local-music-resource-fixture");
    Require(mediaServer.Origin().empty() && mediaServer.Clients() == 0, "image lease releases server resources");
    Require(Read(systemFont) == bytes && fs::last_write_time(systemFont) == sourceWriteTime &&
        Read(font) == bytes && Read(options.root / "font-assets.json") == manifest && picks == 1,
        "WebView decoding does not modify source, persisted metadata or font bytes");
    passed = passed && complete && !timedOut && !result && snapshots >= 1 && fontLookups >= 3 &&
        linkSteps == 2 && !fs::exists(extraLink);
    std::cout << (passed ? "PASS" : "FAIL")
        << ": native real font import/persistence/restore, WebView mapping, parent/iframe decode"
        << ", guarded routes/hardlink read and origin-clean loopback image (phase=" << phase << ")\n";
    return passed;
}
}

int main() {
    fs::path root;
    bool passed = false;
    try {
        root = UniqueTestDirectory(fs::temp_directory_path(), "music-webview-resources-");
        passed = Run(root);
    } catch (const std::exception& error) {
        active = nullptr;
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    if (!root.empty() && fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
        root.filename().u8string().find("music-webview-resources-") == 0) {
        std::error_code error;
        fs::remove_all(root, error);
        if (error) { std::cerr << "FAIL: isolated fixture cleanup\n"; passed = false; }
    }
    return passed ? 0 : 1;
}
