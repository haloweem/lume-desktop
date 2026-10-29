// Checks platform/windows/private_messages.txt against the sources: every
// WM_APP + N literal is registered, every registration still exists, and a
// shared or reserved number is used by no other file.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {
int failures = 0;
void Fail(const std::string& what) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
unsigned long Value(const std::string& text) { return std::stoul(text, nullptr, 0); }
}

int main() {
    const fs::path root = LUME_SOURCE_ROOT;
    // value -> files writing it, from the sources (comments ignored).
    std::map<std::pair<unsigned long, std::string>, std::string> found;
    const std::regex literal(R"(WM_APP\s*\+\s*(0x[0-9a-fA-F]+|[0-9]+))");
    for (const char* top : {"apps", "core", "platform"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root / top)) {
            const auto ext = entry.path().extension();
            if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h")) continue;
            const auto relative = fs::relative(entry.path(), root).generic_string();
            if (relative.find("/upstream/") != std::string::npos) continue;
            std::ifstream in(entry.path());
            for (std::string line; std::getline(in, line);) {
                if (const auto comment = line.find("//"); comment != std::string::npos) line.resize(comment);
                for (std::sregex_iterator m(line.begin(), line.end(), literal), end; m != end; ++m)
                    found[{Value((*m)[1]), relative}] = (*m)[1];
            }
        }
    }
    std::ifstream registry(root / "platform/windows/private_messages.txt");
    if (!registry) { Fail("registry missing"); return 1; }
    std::set<std::pair<unsigned long, std::string>> registered;
    std::map<unsigned long, std::string> exclusive;  // shared/reserved value -> its file
    for (std::string line; std::getline(registry, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string value, scope, file;
        if (!(fields >> value >> scope >> file) || (scope != "local" && scope != "shared" && scope != "reserved")) {
            Fail("bad registry line: " + line); continue;
        }
        const auto number = Value(value);
        if (scope != "reserved") registered.insert({number, file});
        if (scope != "local") {
            if (exclusive.count(number)) Fail("value registered as shared twice: " + value);
            exclusive[number] = file;
        }
    }
    for (const auto& [key, text] : found)
        if (!registered.count(key)) Fail("unregistered WM_APP+" + text + " in " + key.second);
    for (const auto& key : registered)
        if (!found.count(key)) Fail("registered but not in source: " + std::to_string(key.first) + " " + key.second);
    for (const auto& [key, text] : found) {
        const auto owner = exclusive.find(key.first);
        if (owner != exclusive.end() && owner->second != key.second)
            Fail("WM_APP+" + text + " in " + key.second + " collides with the shared message of " + owner->second);
    }
    if (!failures) std::cout << "private messages passed: " << found.size() << " registered\n";
    return failures ? 1 : 0;
}
