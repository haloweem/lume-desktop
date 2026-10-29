#include "core/weather/weather.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
namespace lume::weather {
namespace {
using Json = nlohmann::json;
[[noreturn]] void Invalid() { throw std::runtime_error("Invalid public weather page"); }
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }
std::string_view Trim(std::string_view text) {
    while (!text.empty() && Space(text.front())) text.remove_prefix(1);
    while (!text.empty() && Space(text.back())) text.remove_suffix(1);
    return text;
}
std::string Lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
void Utf8(std::string& out, unsigned code) {
    if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) Invalid();
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) {
        out += static_cast<char>(0xc0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 63));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xe0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 63));
        out += static_cast<char>(0x80 | (code & 63));
    } else {
        out += static_cast<char>(0xf0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 63));
        out += static_cast<char>(0x80 | ((code >> 6) & 63)); out += static_cast<char>(0x80 | (code & 63));
    }
}
std::string Entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            const auto end = text.find(';', i + 1);
            if (end != std::string_view::npos && end - i <= 12) {
                const auto entity = text.substr(i + 1, end - i - 1);
                unsigned code = 0;
                if (entity == "amp") code = '&';
                else if (entity == "lt") code = '<';
                else if (entity == "gt") code = '>';
                else if (entity == "quot") code = '"';
                else if (entity == "apos") code = '\'';
                else if (entity == "nbsp") code = ' ';
                else if (entity == "deg") code = 176;
                else if (entity.size() > 1 && entity.front() == '#') {
                    const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
                    auto digits = entity.substr(hex ? 2 : 1);
                    if (digits.empty() || digits.size() > 8) Invalid();
                    for (const char c : digits) {
                        const int n = c >= '0' && c <= '9' ? c - '0' :
                            hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                            hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                        if (n < 0 || code > 0x10ffff) Invalid();
                        code = code * (hex ? 16u : 10u) + static_cast<unsigned>(n);
                    }
                    if (code == 0) Invalid();
                }
                if (code) { Utf8(out, code); i = end; continue; }
            }
        }
        out += text[i];
    }
    return std::string(Trim(out));
}

// A bounded read-only tokenizer for the provider's data fields. It neither renders
// HTML nor evaluates JavaScript, and keeps views into the caller's single payload.
class Page {
public:
    struct Node {
        std::string tag, classes, src, href;
        std::size_t begin = 0, content = 0, end = 0;
        std::string_view raw;
    };
    explicit Page(std::string_view html) : html_(html) {
        nodes.push_back({"root", "", "", "", 0, 0, html.size(), {}});
        std::vector<std::size_t> stack{0};
        std::size_t at = 0;
        while (at < html.size()) {
            if (html[at] != '<') {
                const auto end = html.find('<', at);
                const auto length = (end == html.npos ? html.size() : end) - at;
                pieces_.push_back({at, html.substr(at, length)});
                at += length;
                continue;
            }
            if (html.substr(at, 4) == "<!--") {
                const auto end = html.find("-->", at + 4);
                if (end == html.npos) Invalid();
                at = end + 3;
                continue;
            }
            const auto start = at++;
            if (at == html.size()) Invalid();
            if (html[at] == '!' || html[at] == '?') {
                const auto end = html.find('>', at);
                if (end == html.npos) Invalid();
                at = end + 1;
                continue;
            }
            const bool close = html[at] == '/';
            if (close) ++at;
            const auto nameStart = at;
            while (at < html.size() && (std::isalnum(static_cast<unsigned char>(html[at])) || html[at] == '-' || html[at] == ':')) ++at;
            if (at == nameStart) Invalid();
            const auto tag = Lower(html.substr(nameStart, at - nameStart));
            Node node{tag, "", "", "", start, 0, html.size(), {}};
            bool selfClosing = false;
            for (;;) {
                while (at < html.size() && Space(html[at])) ++at;
                if (at == html.size()) Invalid();
                if (html[at] == '>') { ++at; break; }
                if (html[at] == '/' && at + 1 < html.size() && html[at + 1] == '>') {
                    at += 2; selfClosing = true; break;
                }
                const auto attrStart = at;
                while (at < html.size() && !Space(html[at]) && html[at] != '=' && html[at] != '>' && html[at] != '/') ++at;
                if (at == attrStart) Invalid();
                const auto attr = Lower(html.substr(attrStart, at - attrStart));
                while (at < html.size() && Space(html[at])) ++at;
                std::string_view value;
                if (at < html.size() && html[at] == '=') {
                    ++at;
                    while (at < html.size() && Space(html[at])) ++at;
                    if (at == html.size()) Invalid();
                    if (html[at] == '"' || html[at] == '\'') {
                        const char quote = html[at++];
                        const auto end = html.find(quote, at);
                        if (end == html.npos) Invalid();
                        value = html.substr(at, end - at); at = end + 1;
                    } else {
                        const auto valueStart = at;
                        while (at < html.size() && !Space(html[at]) && html[at] != '>') ++at;
                        value = html.substr(valueStart, at - valueStart);
                    }
                }
                if (attr == "class" && node.classes.empty()) node.classes = Entities(value);
                if (attr == "src" && node.src.empty()) node.src = Entities(value);
                if (attr == "href" && node.href.empty()) node.href = Entities(value);
            }
            if (close) {
                for (std::size_t i = stack.size(); i > 1; --i) {
                    if (nodes[stack[i - 1]].tag == tag) {
                        for (std::size_t j = i - 1; j < stack.size(); ++j) nodes[stack[j]].end = start;
                        stack.resize(i - 1);
                        break;
                    }
                }
                continue;
            }
            if (nodes.size() >= 16384 || stack.size() >= 128) Invalid();
            node.content = at;
            const auto index = nodes.size();
            nodes.push_back(std::move(node));
            if (tag == "script" || tag == "style" || tag == "textarea" || tag == "title") {
                const auto marker = "</" + tag;
                auto end = at;
                for (;;) {
                    end = html.find('<', end);
                    if (end == html.npos) Invalid();
                    if (Lower(html.substr(end, marker.size())) == marker &&
                        end + marker.size() < html.size() &&
                        (Space(html[end + marker.size()]) || html[end + marker.size()] == '>')) break;
                    ++end;
                }
                nodes[index].raw = html.substr(at, end - at);
                nodes[index].end = end;
                const auto closing = html.find('>', end);
                if (closing == html.npos) Invalid();
                at = closing + 1;
            } else {
                const bool isVoid = tag == "area" || tag == "base" || tag == "br" || tag == "col" ||
                    tag == "embed" || tag == "hr" || tag == "img" || tag == "input" || tag == "link" ||
                    tag == "meta" || tag == "param" || tag == "source" || tag == "track" || tag == "wbr";
                if (selfClosing || isVoid) nodes[index].end = at;
                else stack.push_back(index);
            }
        }
    }
    std::vector<std::size_t> All(std::size_t parent, std::string_view selector) const {
        const auto& scope = nodes.at(parent);
        std::vector<std::size_t> result;
        for (std::size_t i = parent + 1; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (node.begin >= scope.end) break;
            if (node.begin < scope.content) continue;
            if (selector.front() != '.') {
                if (node.tag == selector) result.push_back(i);
            } else {
                auto classes = std::string_view(node.classes);
                while (!(classes = Trim(classes)).empty()) {
                    const auto end = classes.find_first_of(" \t\r\n\f");
                    if (classes.substr(0, end) == selector.substr(1)) { result.push_back(i); break; }
                    if (end == classes.npos) break;
                    classes.remove_prefix(end);
                }
            }
        }
        return result;
    }
    std::size_t One(std::size_t parent, std::string_view selector) const {
        const auto matches = All(parent, selector);
        if (matches.empty()) Invalid();
        return matches.front();
    }
    std::string Text(std::size_t index) const {
        const auto& node = nodes.at(index);
        std::string result;
        for (const auto& piece : pieces_) {
            if (piece.first >= node.end) break;
            if (piece.first >= node.content) result.append(piece.second);
        }
        return Entities(result);
    }
    std::vector<Node> nodes;
private:
    std::string_view html_;
    std::vector<std::pair<std::size_t, std::string_view>> pieces_;
};
// The city above a district page, without the "市" suffix ("广州" for
// tianhe-101280109); empty for a city's own page or when the page has no
// such link. QWeather IDs are 101 + province (2) + city (2) + district (2);
// a city's own ID ends in 01. The four municipalities' city is the
// administrative area shown on the page ("北京市 - 中国").
std::string ParentCity(const Page& page, std::string_view slug, std::string_view district) {
    const auto dash = slug.rfind('-');
    if (dash == slug.npos) return {};
    const auto id = slug.substr(dash + 1);
    if (id.size() != 9 || id.substr(0, 3) != "101" ||
        !std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
    std::string city;
    const auto province = id.substr(3, 2);
    if (province == "01" || province == "02" || province == "03" || province == "04") {
        const auto adm = page.All(0, ".c-submenu__location-adm");
        if (adm.empty()) return {};
        const auto text = page.Text(adm.front());
        city = std::string(Trim(std::string_view(text).substr(0, text.find(" - "))));
    } else {
        if (id.substr(7, 2) == "01") return {};
        const auto suffix = "-" + std::string(id.substr(0, 7)) + "01.html";
        for (const auto anchor : page.All(0, "a")) {
            const auto& href = page.nodes[anchor].href;
            if (href.size() <= suffix.size() || href.compare(href.size() - suffix.size(), suffix.size(), suffix)) continue;
            const auto names = page.All(anchor, "p");
            if (!names.empty()) city = std::string(Trim(page.Text(names.front())));
            break;
        }
    }
    const std::string_view shi = u8"市";
    if (city.size() > shi.size() && city.compare(city.size() - shi.size(), shi.size(), shi) == 0)
        city.resize(city.size() - shi.size());
    // Bounded, and never a repeat of the district itself ("北京" for 北京).
    if (city.empty() || city.size() > 30 || district.substr(0, city.size()) == city) return {};
    return city;
}
double Numeric(std::string_view raw, double min, double max) {
    auto text = Trim(raw);
    for (auto suffix : {std::string_view(u8"°"), std::string_view("%"), std::string_view(u8"℃")})
        if (text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix) {
            text.remove_suffix(suffix.size()); break;
        }
    if (text.empty() || text.size() > 32) Invalid();
    std::size_t i = text.front() == '-' ? 1 : 0;
    const auto first = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
    if (i == first) Invalid();
    if (i < text.size() && text[i] == '.') {
        const auto fraction = ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (i == fraction) Invalid();
    }
    if (i != text.size()) Invalid();
    const double value = std::stod(std::string(text));
    if (!std::isfinite(value) || value < min || value > max) Invalid();
    return value;
}
struct State { int code; std::string label; bool day; };
State Condition(int code, int hour) {
    struct Mapping { int wmo; const char* label; };
    static const std::map<int, Mapping> states{
        {100,{0,u8"晴"}},{101,{2,u8"多云"}},{102,{1,u8"少云"}},{103,{2,u8"晴间多云"}},{104,{3,u8"阴"}},
        {150,{0,u8"晴"}},{151,{2,u8"多云"}},{152,{1,u8"少云"}},{153,{2,u8"晴间多云"}},
        {300,{80,u8"阵雨"}},{301,{82,u8"强阵雨"}},{302,{95,u8"雷阵雨"}},{303,{95,u8"强雷阵雨"}},
        {304,{99,u8"雷阵雨伴有冰雹"}},{305,{61,u8"小雨"}},{306,{63,u8"中雨"}},{307,{65,u8"大雨"}},
        {308,{65,u8"极端降雨"}},{309,{51,u8"毛毛雨"}},{310,{65,u8"暴雨"}},{311,{65,u8"大暴雨"}},
        {312,{65,u8"特大暴雨"}},{313,{67,u8"冻雨"}},{314,{63,u8"小到中雨"}},{315,{65,u8"中到大雨"}},
        {316,{65,u8"大到暴雨"}},{317,{65,u8"暴雨到大暴雨"}},{318,{65,u8"大暴雨到特大暴雨"}},
        {350,{80,u8"阵雨"}},{351,{82,u8"强阵雨"}},{399,{61,u8"雨"}},
        {400,{71,u8"小雪"}},{401,{73,u8"中雪"}},{402,{75,u8"大雪"}},{403,{75,u8"暴雪"}},
        {404,{73,u8"雨夹雪"}},{405,{73,u8"雨雪天气"}},{406,{85,u8"阵雨夹雪"}},{407,{85,u8"阵雪"}},
        {408,{73,u8"小到中雪"}},{409,{75,u8"中到大雪"}},{410,{75,u8"大到暴雪"}},
        {456,{85,u8"阵雨夹雪"}},{457,{85,u8"阵雪"}},{499,{71,u8"雪"}},
        {500,{45,u8"薄雾"}},{501,{45,u8"雾"}},{502,{999,u8"霾"}},{503,{999,u8"扬沙"}},
        {504,{999,u8"浮尘"}},{507,{999,u8"沙尘暴"}},{508,{999,u8"强沙尘暴"}},
        {509,{45,u8"浓雾"}},{510,{45,u8"强浓雾"}},{511,{999,u8"中度霾"}},
        {512,{999,u8"重度霾"}},{513,{999,u8"严重霾"}},{514,{45,u8"大雾"}},{515,{45,u8"特强浓雾"}},
        {900,{999,u8"热"}},{901,{999,u8"冷"}}
    };
    const auto found = states.find(code);
    const bool night = code == 150 || code == 151 || code == 152 || code == 153 ||
        code == 350 || code == 351 || code == 456 || code == 457;
    return {found == states.end() ? 999 : found->second.wmo,
        found == states.end() ? ConditionLabel(999) : found->second.label, !night && hour >= 6 && hour < 18};
}
int Icon(const Page& page, std::size_t index) {
    auto src = std::string_view(page.nodes.at(index).src);
    src = src.substr(0, src.find('?'));
    const auto slash = src.rfind('/');
    if (slash == src.npos) Invalid();
    src.remove_prefix(slash + 1);
    if (src.size() != 7 || src.substr(3) != ".png") Invalid();
    const auto value = Numeric(src.substr(0, 3), 100, 999);
    if (value != std::floor(value)) Invalid();
    return static_cast<int>(value);
}
Json Hours(const Page& page) {
    for (auto index : page.All(0, "script")) {
        auto raw = page.nodes[index].raw;
        while (!raw.empty()) {
            const auto newline = raw.find('\n');
            auto line = Trim(raw.substr(0, newline));
            constexpr std::string_view assignment = "window.hour24";
            if (line.substr(0, assignment.size()) == assignment) {
                line = Trim(line.substr(assignment.size()));
                if (!line.empty() && line.front() == '=') {
                    line = Trim(line.substr(1));
                    const auto end = line.rfind(']');
                    if (!line.empty() && line.front() == '[' && end != line.npos &&
                        Trim(line.substr(end + 1)).substr(0, 1) == ";") {
                        const auto literal = line.substr(0, end + 1);
                        const auto result = Json::parse(literal.begin(), literal.end(),
                            [](int depth, Json::parse_event_t, Json&) { if (depth > 8) Invalid(); return true; });
                        if (!result.is_array() || result.size() < 12 || result.size() > 48) Invalid();
                        return result;
                    }
                }
            }
            if (newline == raw.npos) break;
            raw.remove_prefix(newline + 1);
        }
    }
    Invalid();
}
}
std::optional<Snapshot> ParseQweather(std::string_view html, const Location& location, std::string_view cityId,
    Millis now, Millis fetchedAt, std::string_view slug) {
    try {
        if (html.empty() || html.size() > ProviderMaxBytes || !ValidLocation(location) || !ValidCity(cityId) ||
            now < 0 || now > 253402300799999LL || fetchedAt < 0 || fetchedAt > now) return {};
        const Page page(html);
        const auto current = page.One(0, ".c-city-weather-current");
        auto observed = page.Text(page.One(current, ".current-time"));
        if (observed.size() != 16 || observed[10] != ' ') return {};
        observed[10] = 'T';
        const auto observedAt = ParseLocalMinute(observed, 480);
        if (!observedAt || *observedAt > now + 10 * 60000 || now - *observedAt > 2 * Hour) return {};
        Snapshot result;
        result.cityId = cityId;
        result.cityName = page.Text(page.One(0, ".c-submenu__location"));
        if (const auto city = ParentCity(page, slug, result.cityName); !city.empty())
            result.cityName = city + u8" · " + result.cityName;
        result.location = location;
        result.location.name = result.cityName;
        result.location.timezone = result.timezone = "Asia/Shanghai";
        result.source = Source::Qweather;
        result.updatedAt = fetchedAt;
        result.sourceUpdatedAt = result.validAt = *observedAt;
        const auto live = page.All(current, ".current-live__item");
        if (live.size() < 2) return {};
        const auto fields = page.All(live[1], "p");
        if (fields.size() < 2) return {};
        result.temperature = Numeric(page.Text(fields[0]), -100, 70);
        result.condition = page.Text(fields[1]);
        bool humidity = false;
        for (auto node : page.All(current, ".current-basic___item")) {
            const auto values = page.All(node, "p");
            if (values.size() < 2) continue;
            const auto label = page.Text(values[1]);
            if (label == u8"相对湿度") { result.humidity = Numeric(page.Text(values[0]), 0, 100); humidity = true; }
            if (label == u8"体感温度") result.apparentTemperature = Numeric(page.Text(values[0]), -120, 100);
        }
        if (!humidity) return {};
        const auto icon = page.One(page.One(current, ".current-live"), "img");
        const auto state = Condition(Icon(page, icon), std::stoi(observed.substr(11, 2)));
        result.weatherCode = state.code; result.isDay = state.day;
        const auto hours = Hours(page);
        Millis first = 0;
        std::size_t index = 0;
        for (const auto& item : hours) {
            const auto label = item.at("time").get<std::string>();
            if (label.size() < 3 || label.size() > 4) return {};
            const auto suffix = label.substr(label.size() - 2);
            if (suffix != "am" && suffix != "pm") return {};
            const auto digits = label.substr(0, label.size() - 2);
            if (digits.size() == 2 && digits.front() != '1') return {};
            const int number = static_cast<int>(Numeric(digits, 0, 12));
            const int hour = number % 12 + (suffix == "pm" ? 12 : 0);
            const auto matches = [&](Millis time) {
                const auto local = FormatOffset(time, 480);
                if (!local) return false;
                const auto h = std::stoi(local->substr(11, 2));
                return number == 12 ? h % 12 == 0 : h == hour;
            };
            if (index == 0) {
                const auto base = (*observedAt / Hour) * Hour;
                bool found = false;
                for (int i = -6; i < 4; ++i) {
                    if (matches(base + i * Hour)) { first = base + i * Hour; found = true; break; }
                }
                if (!found) return {};
            }
            const auto time = first + static_cast<Millis>(index++) * Hour;
            if (!matches(time)) return {};
            const auto temperature = Numeric(item.at("tmp").get<std::string>(), -100, 70);
            const auto code = Numeric(item.at("code").get<std::string>(), 100, 999);
            if (code != std::floor(code)) return {};
            const auto local = FormatOffset(time, 480);
            if (!local) return {};
            const auto condition = Condition(static_cast<int>(code), std::stoi(local->substr(11, 2)));
            if (time >= (now / Hour) * Hour && result.hourly.size() < 6)
                result.hourly.push_back({*local, temperature, condition.code, condition.label, condition.day});
        }
        if (result.hourly.size() != 6) return {};
        const auto today = FormatOffset(now, 480);
        if (!today) return {};
        const auto midnight = ParseLocalMinute(today->substr(0, 10) + "T00:00", 480);
        const auto rows = page.All(page.One(0, ".c-city-weather-forecast"), ".city-forecast-tabs__row");
        if (rows.size() < 3 || !midnight) return {};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto dateTime = FormatOffset(*midnight + static_cast<Millis>(i) * Day, 480);
            if (!dateTime) return {};
            const auto date = dateTime->substr(0, 10);
            const auto dates = page.All(page.One(rows[i], ".date"), "p");
            if (dates.size() < 2 || page.Text(dates[1]) != date.substr(5, 2) + u8"月" + date.substr(8) + u8"日") return {};
            const auto temps = page.All(rows[i], ".temp");
            if (temps.size() < 2) return {};
            const auto high = Numeric(page.Text(temps[0]), -100, 70), low = Numeric(page.Text(temps[1]), -100, high);
            const auto condition = Condition(Icon(page, page.One(rows[i], ".icon")), 12);
            result.forecast.push_back({date, high, low, condition.code, condition.label});
        }
        if (!ValidSnapshot(result)) return {};
        return result;
    } catch (const std::exception&) { return {}; }
}
}
