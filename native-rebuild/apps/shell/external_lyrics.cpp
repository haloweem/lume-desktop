#include "apps/shell/external_lyrics.h"
#include "platform/windows/music_online_provider.h"
#include "platform/windows/text_util.h"
#include "core/music/music_online_lyrics.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

namespace lume::shell {

std::wstring LyricLineAt(const std::vector<music::LyricLine>& lines, double position) {
    const music::LyricLine* current = nullptr;
    for (const auto& line : lines) { if (line.startTime > position + .15) break; current = &line; }
    if (!current) return {};
    const auto end = current->endTime > current->startTime ? current->endTime : current->startTime + 8;
    if (position > end + 3) return {};
    return text::Wide(current->fullText);
}

dynamic_island::LyricCues LyricCuesOf(const std::vector<music::LyricLine>& lines) {
    if (lines.empty()) return nullptr;
    auto cues = std::make_shared<std::vector<dynamic_island::LyricCue>>();
    cues->reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        const double next = i + 1 < lines.size() ? lines[i + 1].startTime : line.startTime + 8;
        cues->push_back({line.startTime, line.endTime > line.startTime ? line.endTime : next, text::Wide(line.fullText)});
    }
    return cues;
}

namespace {
using music::auth::Json;
using music::auth::Provider;

// Titles and artists compared without case, spaces, punctuation and bracketed
// remarks ("晴天 (Live)", "稻香（伴奏）" -> the song itself).
std::wstring Normal(std::wstring_view value) {
    std::wstring result;
    int depth = 0;
    for (const wchar_t c : value) {
        if (c == L'(' || c == L'（' || c == L'[' || c == L'【' || c == L'<' || c == L'《') { ++depth; continue; }
        if (c == L')' || c == L'）' || c == L']' || c == L'】' || c == L'>' || c == L'》') { if (depth) --depth; continue; }
        if (depth || std::iswspace(c) || std::iswpunct(c) || c == L'·' || c == L'・' || c == L'、') continue;
        result.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return result;
}
std::vector<std::wstring> Artists(std::wstring_view value) {
    std::vector<std::wstring> result;
    std::wstring part;
    std::wstring all(value);
    all.push_back(L'/');
    for (const wchar_t c : all) {
        if (c == L'/' || c == L',' || c == L'，' || c == L'、' || c == L'&' || c == L';') {
            if (auto n = Normal(part); !n.empty()) result.push_back(std::move(n));
            part.clear();
        } else part.push_back(c);
    }
    return result;
}

struct Request { std::string key; std::wstring title, artist; double duration = 0; std::uint64_t generation = 0; };

// Best catalogue match: the same title is required. Then, in order: an
// artist the player names (catalogues spell some differently, 周杰伦 is
// "Jay" on NetEase, so a different artist only costs), the plain title
// without a remark such as "(深情版)", and a close length. Lengths further
// apart than 8 s are another recording whose timing would not fit.
struct Candidate { Provider provider = Provider::Netease; std::string id; double length = 0, score = 0; };
std::optional<Candidate> Match(const Json& page, const Request& request) {
    if (!page.is_object() || !page.contains("tracks") || !page["tracks"].is_array()) return {};
    const auto title = Normal(request.title);
    const auto artists = Artists(request.artist);
    std::optional<Candidate> best;
    for (const auto& item : page["tracks"]) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("title") ||
            !item["title"].is_string()) continue;
        const auto raw = text::Wide(item["title"].get<std::string>());
        if (title.empty() || Normal(raw) != title) continue;
        double score = 0;
        if (!artists.empty()) {
            const auto candidate = Artists(text::Wide(item.value("artist", std::string{})));
            bool shared = false;
            for (const auto& x : artists) for (const auto& y : candidate)
                if (x == y || (x.size() > 1 && y.find(x) != y.npos) || (y.size() > 1 && x.find(y) != x.npos)) shared = true;
            if (!shared) score += 100;
        }
        if (raw.find_first_of(L"(（[【") != raw.npos && request.title.find_first_of(L"(（[【") == std::wstring::npos) score += 20;
        const double length = item.value("duration", 0.0);
        if (request.duration > 1 && length > 1) {
            const double gap = std::abs(length - request.duration);
            if (gap > 8) continue;
            score += gap;
        }
        if (!best || score < best->score) best = Candidate{{}, item["id"].get<std::string>(), length, score};
    }
    return best;
}
}  // namespace

struct ExternalLyrics::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false, done = false;
    std::optional<Request> pending;
    std::atomic<std::uint64_t> generation{0};
    // Results by track, oldest first out. A network failure retries after a minute.
    struct Entry { dynamic_island::LyricCues cues; bool failed = false; ULONGLONG at = 0; };
    std::map<std::string, Entry> cache;
    std::deque<std::string> order;

    // UI-thread state: the current track and its clock.
    std::string key;
    bool requested = false;
    bool timed = false;  // the clock started with the track
    double clock = 0;
    ULONGLONG clockAt = 0;
    bool playing = false;

    std::vector<music::LyricLine> Find(const Request& request) {
        const auto cancelled = [this, g = request.generation] { return stop || generation.load() != g; };
        const auto query = text::Utf8(request.artist.empty() ? request.title : request.title + L" " + request.artist);
        if (query.empty()) return {};
        // NetEase lacks some catalogues (it lists covers for Jay Chou), KuGou
        // has them: a match with the named artist is taken at once, otherwise
        // every catalogue is asked and the best tried first.
        std::vector<Candidate> found;
        for (const auto provider : {Provider::Netease, Provider::Kugou, Provider::QQ}) {
            try {
                const auto page = music::online::Execute(provider, "search", {{"query", query}, {"limit", 10}}, nullptr, cancelled);
                auto match = Match(page, request);
                if (!match) continue;
                match->provider = provider;
                found.push_back(*match);
                if (match->score < 100) break;
            } catch (...) {
                if (cancelled()) throw;
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const Candidate& x, const Candidate& y) { return x.score < y.score; });
        for (const auto& match : found) {
            try {
                const auto raw = music::online::Execute(match.provider, "lyrics", {{"id", match.id}}, nullptr, cancelled);
                auto result = music::ParseOnlineLyrics(match.provider, raw, match.length);
                if (result.error.empty() && !result.lines.empty() && music::ValidateLyrics(result.lines)) return std::move(result.lines);
            } catch (...) {
                if (cancelled()) throw;
            }
        }
        return {};
    }
    void Run() {
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stop || pending; });
                if (stop) return;
                request = std::move(*pending);
                pending.reset();
            }
            Entry entry;
            try { entry.cues = LyricCuesOf(Find(request)); }
            catch (...) { entry.failed = true; }
            // A lookup cut short by a newer track is not a result.
            if (generation.load() != request.generation) { std::lock_guard lock(mutex); if (stop) return; continue; }
            entry.failed = entry.failed || !entry.cues;
            entry.at = GetTickCount64();
            std::lock_guard lock(mutex);
            if (stop) return;
            if (!cache.count(request.key)) {
                order.push_back(request.key);
                while (order.size() > 32) { cache.erase(order.front()); order.pop_front(); }
            }
            cache[request.key] = std::move(entry);
        }
    }
};

ExternalLyrics::ExternalLyrics() : impl_(std::make_shared<Impl>()) {
    worker_ = std::thread([state = impl_] {
        state->Run();
        std::lock_guard lock(state->mutex);
        state->done = true;
        state->wake.notify_all();
    });
}
ExternalLyrics::~ExternalLyrics() {
    std::unique_lock lock(impl_->mutex);
    impl_->stop = true;
    ++impl_->generation;
    impl_->wake.notify_all();
    // A lookup blocked on the network must not stall exit; the worker owns its state.
    const bool finished = impl_->wake.wait_for(lock, std::chrono::milliseconds(1500), [this] { return impl_->done; });
    lock.unlock();
    if (finished) worker_.join();
    else worker_.detach();
}

void ExternalLyrics::Clear() {
    auto& s = *impl_;
    s.key.clear();
    s.timed = false;
}

void ExternalLyrics::Apply(dynamic_island::Media& media) {
    auto& s = *impl_;
    const auto now = GetTickCount64();
    if (!media.available || media.title.empty()) { Clear(); return; }
    const auto key = media.app + "\n" + text::Utf8(media.title) + "\n" + text::Utf8(media.artist);
    if (key != s.key) {
        // A track that changed while Lume watched starts at zero; one already
        // playing at startup has an unknown position.
        s.timed = !s.key.empty();
        s.key = key;
        s.requested = false;
        s.clock = 0;
        s.clockAt = now;
        s.playing = media.playing;
    }
    if (s.playing) s.clock += static_cast<double>(now - s.clockAt) / 1000;
    s.clockAt = now;
    s.playing = media.playing;

    dynamic_island::LyricCues cues;
    {
        std::lock_guard lock(s.mutex);
        const auto found = s.cache.find(key);
        const bool retry = found != s.cache.end() && found->second.failed && now - found->second.at > 60000;
        if (found != s.cache.end() && !retry) cues = found->second.cues;
        else if (!s.requested || retry) {
            s.requested = true;
            if (retry) found->second.at = now;
            s.pending = Request{key, media.title, media.artist, media.duration, ++s.generation};
            s.wake.notify_all();
        }
    }
    if (!cues) return;
    // A timeline from the player wins; otherwise the clock, if it started with the track.
    if (media.duration <= 0) {
        if (!s.timed) return;
        media.position = s.clock;
    }
    media.lyrics = std::move(cues);
}

}  // namespace lume::shell
