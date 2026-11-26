#pragma once
#include "platform/windows/dock/dock_slots.h"
#include <algorithm>
#include <vector>

namespace lume::platform::windows::dock_animation {
struct Destination { RECT rect{}; LONG edge = 0; };
constexpr LONG Capacity = 512;
constexpr LONG CollectionWindow = 3, CollectionProgram = 4;
constexpr LONG RunningArea = 1; // Slot.reserved in the animation table.
inline UINT ActivatedMessage() {
    static const UINT message = RegisterWindowMessageW(L"Lume.Dock.Activated.v1");
    return message;
}
struct Targets {
    volatile LONG sequence;
    LONG count, bar, icons, edge;
    dock_slots::Slot slots[Capacity];
    volatile LONG click;
    DWORD clickTime;
    ULONGLONG clickProgram, clickWindow;
    Destination clickDestination;
};
inline std::wstring Mapping(HMONITOR monitor, DWORD owner) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return {};
    return L"Local\\Lume.Dock.Genie.v2." + std::to_wstring(owner) + L"." +
           std::to_wstring(dock_slots::ProgramKey(info.szDevice));
}
class Publisher {
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    ~Publisher() { Reset(); }

    void Reset() {
        if (table_) {
            // A monitor reassignment may already have published the other
            // projection here. Only withdraw the bar we last published.
            if (table_->bar == bar_) {
                InterlockedIncrement(&table_->sequence);
                table_->count = 0;
                InterlockedExchange(&table_->click, 0);
                InterlockedIncrement(&table_->sequence);
            }
            UnmapViewOfFile(table_);
        }
        if (mapping_) CloseHandle(mapping_);
        monitor_ = nullptr;
        table_ = nullptr;
        mapping_ = nullptr;
    }
    void Publish(HMONITOR monitor, const Targets& source) {
        if (monitor_ != monitor) Reset();
        if (!table_) {
            const auto name = Mapping(monitor, GetCurrentProcessId());
            if (name.empty()) return;
            mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Targets), name.c_str());
            if (!mapping_) return;
            table_ = static_cast<Targets*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, sizeof(Targets)));
            if (!table_) { Reset(); return; }
            monitor_ = monitor;
        }
        InterlockedIncrement(&table_->sequence);
        table_->bar = bar_ = source.bar;
        table_->icons = source.icons;
        table_->edge = source.edge;
        table_->count = std::clamp(source.count, 0L, Capacity);
        std::copy_n(source.slots, table_->count, table_->slots);
        InterlockedIncrement(&table_->sequence);
    }
    bool RecordClick(ULONGLONG program, ULONGLONG window, RECT rect) {
        if (!table_) return false;
        InterlockedExchange(&table_->click, 0);
        if ((!program && !window) || IsRectEmpty(&rect)) return false;
        const POINT center{(rect.left + rect.right)/2, (rect.top + rect.bottom)/2};
        const HWND hit = GetAncestor(WindowFromPhysicalPoint(center), GA_ROOT);
        if (hit != LongToHandle(table_->icons) && hit != LongToHandle(table_->bar)) return false;
        table_->clickProgram = program;
        table_->clickWindow = window;
        table_->clickDestination = {rect, table_->edge};
        table_->clickTime = GetTickCount();
        if (!++clickSerial_) ++clickSerial_;
        InterlockedExchange(&table_->click, static_cast<LONG>(clickSerial_));
        return true;
    }
private:
    DWORD clickSerial_ = 0;
    HANDLE mapping_ = nullptr;
    Targets* table_ = nullptr;
    HMONITOR monitor_ = nullptr;
    LONG bar_ = 0;
};

inline bool VisibleTarget(const Targets& table, HMONITOR monitor, DWORD owner,
                          HWND source, ULONGLONG program, Destination& result, bool minimizing) {
    const auto window = reinterpret_cast<UINT_PTR>(source);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const LONG before = table.sequence;
        if (before & 1) { Sleep(1); continue; }
        const HWND bar = static_cast<HWND>(LongToHandle(table.bar));
        const HWND icons = static_cast<HWND>(LongToHandle(table.icons));
        std::vector<dock_slots::Slot> running, groups, direct;
        const LONG edge = table.edge;
        for (LONG i = 0, count = std::clamp(table.count, 0L, Capacity); i < count; ++i) {
            const auto slot = table.slots[i];
            if ((slot.kind == CollectionWindow && window && slot.key == window) ||
                (slot.kind == CollectionProgram && program && slot.key == program)) groups.push_back(slot);
            else if ((slot.kind == dock_slots::WindowSlot && window && slot.key == window) ||
                     (slot.kind == dock_slots::ProgramSlot && program && slot.key == program)) (slot.reserved == RunningArea ? running : direct).push_back(slot);
        }
        MemoryBarrier();
        if (before != table.sequence) continue;
        if ((running.empty() && groups.empty() && direct.empty()) || edge < 0 || edge > 3) return false;
        DWORD iconPid = 0, barPid = 0;
        GetWindowThreadProcessId(icons, &iconPid);
        GetWindowThreadProcessId(bar, &barPid);
        if (iconPid != owner || barPid != owner || !IsWindowVisible(icons) || IsIconic(icons) ||
            !IsWindowVisible(bar) || IsIconic(bar)) return false;
        // A visible running tile owns both directions, including program-only matches.
        for (auto* candidates : {&running, &direct, &groups}) {
            std::stable_sort(candidates->begin(), candidates->end(), [](const auto& a, const auto& b) { return a.kind < b.kind; });
            for (const auto& match : *candidates) {
                const RECT rect{match.left, match.top, match.right, match.bottom};
                if (IsRectEmpty(&rect)) continue;
                const POINT center{(rect.left + rect.right)/2, (rect.top + rect.bottom)/2};
                const HWND hit = GetAncestor(WindowFromPhysicalPoint(center), GA_ROOT);
                // A normal desktop-layer Dock is behind the window being minimized.
                // Its own window (or an owned shadow) can cover the landing point.
                const bool leaving = minimizing && hit &&
                    (hit == source || GetAncestor(hit, GA_ROOTOWNER) == source);
                if ((hit == icons || hit == bar || leaving) && MonitorFromPoint(center, MONITOR_DEFAULTTONULL) == monitor) {
                    result = {rect, edge};
                    return true;
                }
            }
        }
        return false;
    }
    return false;
}
inline bool Find(HWND source, DWORD owner, Destination& result, bool consumeClick = false, bool clickOnly = false,
                 bool minimizing = false) {
    const auto monitor = MonitorFromWindow(source, MONITOR_DEFAULTTONULL);
    const auto name = Mapping(monitor, owner);
    if (name.empty()) return false;
    const DWORD access = consumeClick ? FILE_MAP_WRITE : FILE_MAP_READ;
    HANDLE mapping = OpenFileMappingW(access, FALSE, name.c_str());
    if (!mapping) return false;
    auto* table = static_cast<Targets*>(MapViewOfFile(mapping, access, 0, 0, sizeof(Targets)));
    bool matched = false;
    if (table) {
        wchar_t path[32768]{};
        const auto program = GetModuleFileNameW(nullptr,path,32768) ? dock_slots::ProgramKey(path) : 0;
        const auto hwnd = reinterpret_cast<UINT_PTR>(source);
        const LONG click = table->click;
        if (consumeClick && click && GetTickCount() - table->clickTime < 3000 &&
            ((program && program == table->clickProgram) || hwnd == table->clickWindow)) {
            const Destination dest = table->clickDestination;
            if (InterlockedCompareExchange(&table->click, 0, click) == click) {
                result = dest;
                matched = true;
            }
        }
        if (!matched && !clickOnly) matched = VisibleTarget(*table, monitor, owner, source, program, result, minimizing);
        UnmapViewOfFile(table);
    }
    CloseHandle(mapping);
    return matched;
}
}
