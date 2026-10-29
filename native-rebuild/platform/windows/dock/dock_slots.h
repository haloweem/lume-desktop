#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// Where things sit in Lume's Dock, published for the minimize animation hook
// running inside other (32- or 64-bit) processes: each window thumbnail by
// its HWND, and each program icon by a hash of its executable path. Fixed-size
// POD only; readers retry while the writer's sequence is odd.
namespace lume::platform::windows::dock_slots {
constexpr wchar_t MappingName[] = L"Local\\Lume.Dock.Slots.v1";
constexpr LONG Capacity = 96;
enum : LONG { WindowSlot = 1, ProgramSlot = 2 };
struct Slot { LONG kind; LONG reserved; ULONGLONG key; LONG left, top, right, bottom; };
struct Table { volatile LONG sequence; LONG count; Slot slots[Capacity]; };

// Same key on both sides: lower-case, backslashes, FNV-1a over UTF-16.
inline ULONGLONG ProgramKey(std::wstring path) {
    if (path.empty()) return 0;
    for (auto& c : path) if (c == L'/') c = L'\\';
    CharLowerBuffW(path.data(), static_cast<DWORD>(path.size()));
    ULONGLONG hash = 1469598103934665603ULL;
    for (const wchar_t c : path) { hash ^= static_cast<unsigned short>(c); hash *= 1099511628211ULL; }
    return hash ? hash : 1;
}

// How a window looked just before it was minimized, published by the minimize
// animation hook: DWM keeps no usable image for some minimized windows (they
// show grey). Top-down premultiplied BGRA, at most SnapshotSide on each side.
constexpr LONG SnapshotSide = 256;
struct SnapshotHeader { LONG width, height; volatile LONG serial; LONG reserved; };
inline std::wstring SnapshotName(ULONGLONG window) {
    wchar_t name[64]{};
    swprintf_s(name, L"Local\\Lume.Dock.Snapshot.v1.%llx", window);
    return name;
}
constexpr DWORD SnapshotBytes = sizeof(SnapshotHeader) + SnapshotSide * SnapshotSide * 4;

// Reader: the screen rectangle for a window, else for its program. False when
// the Dock is not running or has neither.
inline bool Find(HWND window, ULONGLONG program, RECT& out, const wchar_t* name = MappingName) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!mapping) return false;
    const auto* table = static_cast<const Table*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Table)));
    bool found = false;
    if (table) {
        for (int attempt = 0; attempt < 4 && !found; ++attempt) {
            const LONG before = table->sequence;
            if (before & 1) { Sleep(0); continue; }
            Slot match{};
            bool any = false;
            const LONG count = table->count < 0 ? 0 : table->count > Capacity ? Capacity : table->count;
            for (LONG i = 0; i < count; ++i) {
                const auto& s = table->slots[i];
                if (s.kind == WindowSlot && s.key == static_cast<ULONGLONG>(reinterpret_cast<std::uintptr_t>(window))) { match = s; any = true; break; }
                if (!any && program && s.kind == ProgramSlot && s.key == program) { match = s; any = true; }
            }
            MemoryBarrier();
            if (table->sequence != before) continue;
            if (any && match.right > match.left && match.bottom > match.top) {
                out = {match.left, match.top, match.right, match.bottom};
                found = true;
            }
            break;
        }
        UnmapViewOfFile(table);
    }
    CloseHandle(mapping);
    return found;
}
}  // namespace lume::platform::windows::dock_slots
