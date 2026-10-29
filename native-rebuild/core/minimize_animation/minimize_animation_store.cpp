#include "core/minimize_animation/minimize_animation_store.h"
#include "core/settings/fixed_json_reader.h"

namespace lume::minimize_animation {
bool Encode(const Preferences& p, std::string& output) {
    if (!Valid(p)) return false;
    const auto b = [](bool v) { return v ? "true" : "false"; };
    output = std::string("{\"version\":1,\"enabled\":")+b(p.enabled)+
        ",\"durationMs\":"+std::to_string(p.durationMs)+",\"restore\":"+b(p.restore)+
        ",\"launch\":"+b(p.launch)+",\"multiMonitor\":"+b(p.multiMonitor)+
        ",\"unhideTaskbar\":"+b(p.unhideTaskbar)+",\"unhideDurationMs\":"+
        std::to_string(p.unhideDurationMs)+",\"engine\":\""+
        (p.engine == Engine::Modern ? "modern" : "classic")+"\"}";
    return true;
}
bool Decode(std::string_view input, Preferences& output) {
    if (input.empty() || input.size() > MaxPreferencesBytes) return false;
    settings::Reader r(input); if (!r.Take('{')) return false;
    Preferences p; unsigned seen = 0;
    do {
        std::string key; if (!r.String(key) || !r.Take(':')) return false;
        unsigned bit = 0;
        if (key == "version") { bit=1; int v=0; if (!r.Integer(v) || v!=1) return false; }
        else if (key == "enabled") { bit=2; if (!r.Boolean(p.enabled)) return false; }
        else if (key == "durationMs") { bit=4; if (!r.Integer(p.durationMs)) return false; }
        else if (key == "restore") { bit=8; if (!r.Boolean(p.restore)) return false; }
        else if (key == "launch") { bit=16; if (!r.Boolean(p.launch)) return false; }
        else if (key == "multiMonitor") { bit=32; if (!r.Boolean(p.multiMonitor)) return false; }
        else if (key == "unhideTaskbar") { bit=64; if (!r.Boolean(p.unhideTaskbar)) return false; }
        else if (key == "unhideDurationMs") { bit=128; if (!r.Integer(p.unhideDurationMs)) return false; }
        else if (key == "engine") {
            bit=256; std::string engine;
            if (!r.String(engine) || (engine != "modern" && engine != "classic")) return false;
            p.engine = engine == "modern" ? Engine::Modern : Engine::Classic;
        } else return false;
        if (seen & bit) return false;
        seen |= bit;
    } while (r.Take(','));
    if (!r.Take('}') || !r.End() || seen != 511 || !Valid(p)) return false;
    output=p; return true;
}
LoadedPreferences PreferencesStore::Load(const std::string& path) const {
    LoadedPreferences result;
    if (path.empty()) { result.state=LoadState::Failed; return result; }
    std::string text;
    if (files_.Exists(path)) {
        if (files_.ReadBounded(path,MaxPreferencesBytes,text) && Decode(text,result.value)) {
            result.state=LoadState::Loaded; return result;
        }
        result.state=LoadState::Failed;
    }
    if (files_.Exists(path+".bak"))
        result.state=files_.ReadBounded(path+".bak",MaxPreferencesBytes,text) &&
            Decode(text,result.value) ? LoadState::Recovered : LoadState::Failed;
    return result;
}
bool PreferencesStore::Save(const std::string& path,const Preferences& value) {
    std::string encoded; if (path.empty() || !Encode(value,encoded)) return false;
    const bool exists=files_.Exists(path);
    std::string original; Preferences old;
    if (exists && !files_.ReadBounded(path,MaxPreferencesBytes,original)) return false;
    const bool valid=exists && Decode(original,old);
    const auto temp=path+".tmp", backup=path+".bak.tmp";
    const auto fail=[&] { files_.Remove(temp); files_.Remove(backup); return false; };
    if (!files_.Write(temp,encoded)) return fail();
    if (valid && (!files_.Write(backup,original) || !files_.Rename(backup,path+".bak"))) return fail();
    std::string quarantine;
    if (exists && !valid) {
        for (unsigned i=0;i<1024;++i) {
            const auto target=path+".corrupt"+(i ? "."+std::to_string(i) : "");
            if (!files_.Exists(target)) { quarantine=target; break; }
        }
        if (quarantine.empty() || !files_.Rename(path,quarantine)) return fail();
    }
    if (!files_.Rename(temp,path)) {
        if (!quarantine.empty()) files_.Rename(quarantine,path);
        return fail();
    }
    return true;
}
}
