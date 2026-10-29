#include "core/minimize_animation/minimize_animation.h"
#include "core/minimize_animation/minimize_animation_store.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
using namespace lume::minimize_animation;
namespace {
void Check(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
bool Near(float a, float b) { return std::abs(a - b) < .01f; }
struct Files final : lume::settings::IFileStore {
    std::map<std::string, std::string> data;
    std::string fail;
    bool Exists(const std::string& p) const override { return data.count(p) != 0; }
    bool Read(const std::string& p, std::string& s) const override {
        auto it = data.find(p); if (it == data.end()) return false; s = it->second; return true;
    }
    bool Write(const std::string& p, const std::string& s) override {
        if (fail == p) return false;
        data[p] = s;
        return true;
    }
    bool Rename(const std::string& a, const std::string& b) override {
        if (fail == b || !Exists(a)) return false;
        data[b] = data[a];
        data.erase(a);
        return true;
    }
    bool Remove(const std::string& p) override { data.erase(p); return true; }
};
}
int main() {
    Preferences p;
    Check(!p.enabled && p.durationMs == 450 && p.restore && p.launch &&
          !p.multiMonitor && p.unhideTaskbar && p.unhideDurationMs == 450 &&
          p.engine == Engine::Modern, "defaults (the open animation is on)");
    auto bad = p; bad.durationMs = 49; Check(!Valid(bad), "reject short animation");
    bad.durationMs = 2001; Check(!Valid(bad), "reject long animation");
    bad = p; bad.unhideDurationMs = -1; Check(!Valid(bad), "reject negative taskbar duration");
    bad.unhideDurationMs = 5001; Check(!Valid(bad), "reject excessive taskbar duration");
    bad = p; bad.engine = static_cast<Engine>(99); Check(!Valid(bad), "reject invalid engine");
    const Rect window{100, 60, 640, 480}, dock{500, 1000, 22, 33};
    for (float x : {0.f, .3f, 1.f}) for (float y : {0.f, .4f, 1.f}) {
        const auto v = LampVertex(x, y, 0, window, dock);
        Check(Near(v.x, window.x + window.width*x) && Near(v.y, window.y + window.height*y),
              "modern first frame is identity");
        const auto z = LampVertex(x, y, 1, window, dock);
        Check(std::isfinite(z.x) && std::isfinite(z.y) && Near(z.y, dock.y),
              "modern last frame collapses at dock without nan");
    }
    const auto top = LampVertex(0, 0, .2f, window, dock);
    const auto mid = LampVertex(0, .25f, .2f, window, dock);
    const auto bottom = LampVertex(0, 1, .2f, window, dock);
    Check(std::abs(mid.x - (top.x*.75f+bottom.x*.25f)) > 2, "modern curve is non-affine genie warp");
    const auto flat = LampVertex(.5f,.5f,.5f,{0,1000,100,100},dock);
    Check(std::isfinite(flat.x) && std::isfinite(flat.y), "degenerate geometry remains finite");
    const auto c0 = ClassicRow(.25f,0,window,511,1033);
    Check(Near(c0.y,180) && Near(c0.width,640) && Near(c0.center,420), "classic identity");
    const auto cTop = ClassicRow(0,.4f,window,511,1033);
    const auto cBottom = ClassicRow(1,.4f,window,511,1033);
    Check(cBottom.width < cTop.width, "classic bottom rows form neck before top");
    Check(SnapshotBytes(640,480) == 640u*480u*4u &&
          !SnapshotBytes(0,2) && !SnapshotBytes(100000,100000), "snapshot allocation cap");
    Check(ShowCommand(0xbeef0006u)==6 && ShowCommand(0xffff0009u)==9,
          "CBT ignores undocumented high word");
    std::string text;
    Check(Encode(p,text), "encode defaults");
    Preferences decoded; decoded.enabled = true;
    Check(Decode(text,decoded) && decoded == p, "roundtrip source settings");
    const auto original = decoded;
    Check(!Decode(text.substr(0,text.size()-1)+",\"enabled\":true}",decoded) && decoded == original,
          "duplicate fields fail without partially replacing settings");
    Check(!Decode(text.substr(0,text.size()-1)+",\"extra\":0}",decoded), "unknown field rejected");
    auto fractional = text; const auto at = fractional.find("450");
    fractional.replace(at,3,"450.5"); Check(!Decode(fractional,decoded), "fractional integer rejected");
    Files files; PreferencesStore store(files);
    Check(store.Load("p").state == LoadState::Fresh, "missing settings are fresh");
    Check(store.Save("p",p), "first save");
    p.enabled = true; p.engine = Engine::Classic;
    Check(store.Save("p",p) && store.Load("p").value == p && files.Exists("p.bak"),
          "save backs up original");
    files.data["p"] = "broken";
    Check(store.Load("p").state == LoadState::Recovered && !store.Load("p").value.enabled,
          "corrupt primary recovers backup");
    Check(store.Save("p",p) && files.data["p.corrupt"] == "broken", "corrupt settings quarantined");
    auto next = p; next.durationMs = 1200; files.fail = "p";
    Check(!store.Save("p",next) && store.Load("p").value == p && !files.Exists("p.tmp"),
          "failed replacement leaves original and removes temporary");
    std::cout << "minimize animation core passed\n";
}
