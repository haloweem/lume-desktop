#include "core/ui/wallpaper_page_protocol.h"
#include "core/wallpaper/catalog.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include <cstdlib>
#include <iostream>
using namespace lume::ui;
namespace {
void Expect(bool value,const char* why) {
    if(!value){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}
}
}
int main() {
    const auto apply=ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"apply","id":"local-12_a","display":"18446744073709551615"})");
    Expect(apply&&apply->action==WallpaperAction::Apply&&apply->id=="local-12_a"&&apply->display==UINT64_MAX,
        "accept an internal media id and a canonical uint64 monitor id");
    const auto volume=ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"volume","display":"1","value":0})");
    Expect(volume&&volume->volume==0,"muted default is a valid explicit setting");
    const auto fit=ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"fit","display":"1","value":"cover"})");
    Expect(fit&&fit->flag,"original cover crop");
    Expect(ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"preview","id":"local-a"})").has_value(),
        "preview leases require a library or catalog id, not a path");
    Expect(ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"endPreview"})").has_value(),
        "preview release is explicit");
    Expect(ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"stats","id":"official-a"})").has_value(),
        "public engagement selects a safe official ID");
    Expect(ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"endStats"})").has_value(),
        "leaving detail cancels selected engagement");
    const auto like=ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"like","id":"official-a","value":true})");
    Expect(like&&like->id=="official-a"&&like->flag,"favorites declare an explicit desired value");
    Expect(ParseWallpaperPageJson(LR"({"version":1,"scope":"wallpaper","action":"applyAll","id":"official-a"})").has_value(),
        "all-display application accepts only an internal media ID");
    Expect(lume::wallpaper::IsSafeWallpaperId("official.v2")&&ValidWallpaperId("official.v2"),
        "catalog and page protocol share the same safe ID policy");
    const auto dotted=ParseWallpaperPageJson(
        LR"({"version":1,"scope":"wallpaper","action":"download","id":"official.v2"})");
    Expect(dotted&&dotted->id=="official.v2",
        "page commands accept every ID accepted by the catalog");
    for(const auto* input:{
        LR"({"version":1,"scope":"wallpaper","action":"apply","id":"../elsewhere","display":"1"})",
        LR"({"version":1,"scope":"wallpaper","action":"apply","id":"ok","display":"01"})",
        LR"({"version":1,"scope":"wallpaper","action":"apply","id":"ok","display":"18446744073709551616"})",
        LR"({"version":1,"scope":"wallpaper","action":"apply","id":"ok","display":"0"})",
        LR"({"version":1,"scope":"wallpaper","action":"volume","display":"1","value":101})",
        LR"({"version":1,"scope":"wallpaper","action":"volume","display":"1","value":1.5})",
        LR"({"version":1,"scope":"wallpaper","action":"download","id":"ok","url":"https://elsewhere/video"})",
        LR"({"version":1,"scope":"wallpaper","action":"import","path":"C:/private"})",
        LR"({"version":1,"scope":"wallpaper","action":"stopAll","action":"import"})",
        LR"({"version":2,"scope":"wallpaper","action":"snapshot"})",
        LR"({"version":1,"scope":"wallpaper","action":"snapshot","nested":{}})",
        LR"({"version":1,"scope":"wallpaper","action":"pauseFullscreen","value":1})",
        LR"({"version":1,"scope":"wallpaper","action":"like","id":"official-a","value":1})",
        LR"({"version":1,"scope":"wallpaper","action":"stats","id":"../x"})",
        LR"({"version":1,"scope":"wallpaper","action":"stats","id":"official-a","bearer":"secret"})",
        LR"({"version":1,"scope":"wallpaper","action":"endStats","id":"official-a"})",
        LR"({"version":1,"scope":"wallpaper","action":"applyAll","id":"official-a","displays":["1"]})",
        LR"(["wallpaper","snapshot"])"}) {
        Expect(!ParseWallpaperPageJson(input),"reject invalid or over-capable page message");
    }
    Expect(!ParseWallpaperPageJson(std::wstring(2048,L' ')),"bounded input");
    Expect(ValidWallpaperId("official-2026_a")&&!ValidWallpaperId("bad/path")&&!ValidWallpaperId(""),"bounded opaque ids");
    return 0;
}
