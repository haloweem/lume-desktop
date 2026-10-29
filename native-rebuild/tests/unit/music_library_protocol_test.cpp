#include "core/ui/music_library_protocol.h"
#include <iostream>

int main() {
    using lume::ui::ParseMusicLibraryCommand;
    if (!ParseMusicLibraryCommand(LR"({"version":1,"scope":"music-library","action":"snapshot"})") ||
        !ParseMusicLibraryCommand(LR"({"version":1,"scope":"music-library","action":"play","id":"local-123"})") ||
        !ParseMusicLibraryCommand(LR"({"version":1,"scope":"music-library","action":"page","offset":100,"limit":100})"))
        return 1;
    for (const auto* value : {
        LR"({"version":1,"scope":"music-library","action":"play","id":"C:/private.mp3"})",
        LR"({"version":1,"scope":"music-library","action":"play","id":"local-1","path":"secret"})",
        LR"({"version":1,"scope":"music-library","action":"play","id":"local-1","id":"local-2"})",
        LR"({"version":1,"scope":"music-library","action":"play","id":"netease-1"})",
        LR"({"version":1,"scope":"music-library","action":"page","offset":-1,"limit":100})",
        LR"({"version":1,"scope":"music-library","action":"page","offset":0,"limit":101})",
        LR"({"version":1,"scope":"music-library","action":"visibility","visible":1})"})
        if (ParseMusicLibraryCommand(value)) return 1;
    std::cout << "music library protocol passed\n";
}
