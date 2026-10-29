#pragma once
#include "core/organizer/organizer_model.h"
namespace lume::organizer {
struct DesktopEntry {Item item;bool application=false;};
struct DesktopSnapshot {std::vector<DesktopEntry> entries;bool complete=false;};
// One window with Desktop and Files tabs for first use; no desktop I/O here.
State DesktopDefaults();
// Upgrade known desktop/file collections, retaining custom collections and groups.
// A desktop tab the user deleted (State::*Retired) is not created again.
bool EnsureDesktopCollections(State&);
// Organizer reset: brings deleted desktop tabs back.
bool RestoreDesktopCollections(State&);
struct DesktopSync {bool changed=false,limited=false;};
// Only complete desktop snapshots can remove old automatically managed references.
// No files are moved or deleted. Existing references retain IDs and user order.
DesktopSync SyncDesktop(State&,const DesktopSnapshot&,const TextKey&);
}
