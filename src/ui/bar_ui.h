// src/ui/bar_ui.h — BeetleRecomp settings/launcher UI (RmlUi over RT64).
//
// This is the BAR-specific UI layer ported from Zelda64Recompiled's src/ui (MIT), reduced to a
// documents-driven menu rather than Zelda's full recompui element/data-binding framework. It
// renders through RT64 via an injected render hook (the RmlUi->RT64 bridge in ui_renderer.cpp)
// and drives ultramodern's GraphicsConfig + the librecomp mod loader.
//
// Lifecycle:
//   * main() calls install() BEFORE recomp::start(): this registers the RT64 render hooks, so when
//     RT64 finishes setup it builds the UI on the render thread (init hook) and composites it over
//     every presented frame (draw hook).
//   * The launcher is shown first. main() feeds SDL events here while the menu owns input.
//   * When the user clicks "Play", the menu records the request; main()'s coordinator thread reads
//     it via consume_play_request() and runs select_rom()/start_game(), then calls on_game_started()
//     to hide the launcher.

#ifndef BAR_UI_H
#define BAR_UI_H

#include <filesystem>

struct SDL_Window;
union SDL_Event;

namespace bar_ui {

// Register the RT64 render hooks and remember the window + the ROM the launcher's "Play" should
// load. Call once, before recomp::start(). Safe even if UI asset files are missing (the hooks just
// no-op the menu in that case).
void install(SDL_Window* window, const std::filesystem::path& default_rom);

// Queue an SDL event for the UI (drained on the render thread). Call from the main SDL poll loop
// while menu_capturing_input() is true.
void handle_sdl_event(const SDL_Event& event);

// True while the launcher/config menu is shown and should receive input (i.e. the game hasn't been
// started yet, or the in-game menu is open).
bool menu_capturing_input();

// One-shot: returns true exactly once after the user activates "Play", writing the chosen ROM path
// to out_rom. The coordinator thread in main() polls this.
bool consume_play_request(std::filesystem::path& out_rom);

// Tell the UI the game has started so it hides the launcher and releases input to the game.
void on_game_started();

// Toggle the in-game pause menu during gameplay (bound to Esc / F1 / controller Back in main's SDL
// loop). Opening it pauses the game simulation; closing resumes. No-op before the game has started.
// Thread-safe: the actual document show/hide + pause happen on the render thread.
void toggle_pause_menu();

} // namespace bar_ui

#endif // BAR_UI_H
