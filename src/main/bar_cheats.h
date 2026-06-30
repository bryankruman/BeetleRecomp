// src/main/bar_cheats.h — Beetle Adventure Racing cheat toggles (host-side RDRAM pokes).
//
// The cheats menu (src/ui) flips these toggles; apply_frame() — called once per controller poll
// (~per frame) from the SI hook in os_unimpl_stubs.cpp, which has `rdram` in scope — writes the
// corresponding values into the game's RDRAM via the MEM_* macros. Every address is a FIXED global
// in BAR's gGameSettings struct (0x80025CF0) or the open-count / cheat-flag blocks (0x8002CFF0 /
// 0x8002CC94 / 0x8002D009), each cross-validated against lib/bar-decomp's symbol_addrs.txt +
// structs.h + game_init.c. We deliberately avoid the per-race timer/position/boost state: it lives
// at non-fixed heap addresses (relocatable overlays) and a literal-address poke there would crash.
// See the project memory "bar-cheats" for the full vetted address list + risk notes.
#ifndef BAR_CHEATS_H
#define BAR_CHEATS_H

#include <cstdint>

namespace bar_cheats {

// Stable cheat ids. Keep in sync with kInfo[] (bar_cheats.cpp); the UI iterates these in order.
enum class Id : int {
    UnlockAll = 0,    // unlock all cars / tracks / championships / battle arenas
    RevealCheatMenu,  // reveal BAR's own hidden in-game Cheats menu (the Daisy-box cheats)
    SoloRace,         // remove the rival AI cars
    InfiniteLaps,     // race never ends on lap count
    SuperSpeed,       // raise the debug max-speed cap
    SteeringAssist,   // debug steering assist
    ShowFps,          // BAR's on-screen framerate display
    NoGlare,          // disable the sun-glare effect
    Count
};

// UI-facing metadata for building the menu rows.
struct Info { const char* id; const char* label; const char* hint; };
const Info& info(Id id);

void set(Id id, bool enabled);
bool get(Id id);
bool any_enabled();

// Apply all enabled cheats into RDRAM. MUST be called from a scope that has a `uint8_t* rdram`
// (the MEM_* macros dereference it) — i.e. the SI hook. Cheap + idempotent; safe to call per call.
void apply_frame(uint8_t* rdram);

// Persistence (a small cheats.cfg in the per-user config dir). Safe to call before/without the UI.
void load();
void save();

} // namespace bar_cheats

#endif // BAR_CHEATS_H
