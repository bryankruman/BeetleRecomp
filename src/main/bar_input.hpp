// src/main/bar_input.hpp — the live runtime input path (4 N64 controller ports).
//
// This is the single source of truth for what each of BAR's four controller ports reports. It:
//   * holds the live InputConfig (pushed from the Controls UI / startup via set_config) as a
//     thread-safe snapshot,
//   * owns the open SDL_GameController handles and enumerates them for the UI (MAIN THREAD only,
//     preserving the pre-existing "all SDL controller access on one thread" invariant),
//   * samples every assigned gamepad once per frame on the main thread into per-port atomic
//     snapshots (sample_all_ports), and
//   * resolves a port's live N64 buttons + analog stick from its assigned device + bindings
//     (resolve_port) on the game/SI thread, reading only atomics + a config snapshot.
//
// The SI/PIF joybus stub (os_unimpl_stubs.cpp) and the high-level input_get callback both go through
// this module, so input reaches the game whichever way it polls. Pak status (Controller/Rumble) and
// rumble live here too; the mempak store + joybus pak emulation are handled in the SI stub.

#ifndef BAR_MAIN_INPUT_HPP
#define BAR_MAIN_INPUT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "game/input_config.hpp"   // bar::input_config::{InputConfig, PakType}

namespace bar::input {

// Push a new 4-port config into the live path. Called from the Controls UI (render thread) on every
// change and once at startup (main thread). Copies into an atomically-swapped snapshot; cheap to
// call. Does NOT persist (the UI/startup persists via bar::input_config).
void set_config(const bar::input_config::InputConfig& cfg);

// ---- MAIN-THREAD-ONLY: SDL controller lifecycle + per-frame sampling ----
// Called from main.cpp's update_gfx SDL event loop / per-frame tick.
void on_controller_added(int sdl_device_index);       // SDL_CONTROLLERDEVICEADDED .which
void on_controller_removed(int sdl_instance_id);      // SDL_CONTROLLERDEVICEREMOVED .which
void sample_all_ports();                              // refresh per-port gamepad snapshots
void flush_rumble();                                  // issue SDL rumble for ports that want it

// Copied list of currently-connected controllers, for the Controls dialog's device dropdown.
struct DeviceEntry { std::string guid; std::string name; };
std::vector<DeviceEntry> enumerate_devices();

// ---- GAME/SI-THREAD: per-port resolution + presence ----
// Live N64 button mask (+ analog stick in *sx/*sy, N64 range ~[-80,80]) for `port`, resolved from
// its assigned device + bindings. Neutral (0) if the port has no working device. Lock-light: reads
// a config snapshot + atomics only. Does NOT apply the focus/menu/autoplay gates — the caller
// (bar_poll_keyboard) does, since those are global, not per-port.
uint16_t resolve_port(int port, int8_t* sx, int8_t* sy);

// A port is "present" (advertised to the game) when it's plugged in AND has a device assigned.
bool port_connected(int port);
// The pak inserted in a port (None if not present).
bar::input_config::PakType port_pak(int port);

// ---- Rumble ----
// Request rumble on/off for a port (from the input_set_rumble callback / the SI-stub motor
// intercept). Deferred to flush_rumble() on the main thread (SDL rumble must run there).
void set_rumble(int port, bool on);

// ---- Controller Pak (mempak) storage ----
// A port's 32 KiB Controller Pak, emulated at the joybus level by the SI stub. read/write operate on
// one 32-byte block (block = joybus address >> 5). The backing store is lazily loaded from (or created
// in) mempak_p{port}.pak in the per-user config dir; a fresh pak is initialized to a valid formatted-
// empty image. Return false if the block is out of range. Thread-safe (SI thread reads/writes; the
// main thread flushes). These do NOT check the pak type — the SI stub routes only Controller-Pak ports.
bool mempak_read(int port, int block, uint8_t out[32]);
bool mempak_write(int port, int block, const uint8_t in[32]);

// Persist any Controller Paks modified since the last flush. Call periodically on the main thread
// (cheap no-op when nothing is dirty).
void mempak_flush_all();

} // namespace bar::input

#endif // BAR_MAIN_INPUT_HPP
