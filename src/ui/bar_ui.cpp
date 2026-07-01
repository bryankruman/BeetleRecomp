// src/ui/bar_ui.cpp — BeetleRecomp settings/launcher UI implementation. See bar_ui.h.
//
// Ported in spirit from Zelda64Recompiled's src/ui (MIT): same RmlUi-over-RT64 architecture
// (render hook init/draw/deinit + queued SDL input), but documents-driven instead of Zelda's
// recompui element/data-binding framework, and de-coupled from Zelda-specific systems.

#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <locale>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <concurrentqueue.h>

#include "RmlUi/Core.h"
#include "RmlUi/Core/Elements/ElementFormControl.h"
#include "RmlUi/Debugger.h"

#include "RmlUi_Platform_SDL.h"   // SystemInterface_SDL + RmlSDL::InputEventHandler (RmlUi backend)

#include "common/rt64_plume.h"    // plume::Render* (RT64 RHI)
#include "rhi/rt64_render_hooks.h" // RT64::SetRenderHooks

#include "json/json.hpp"          // reuse the canonical enum<->string tables from config.hpp

#include "ultramodern/ultramodern.hpp"  // ultramodern::quit
#include "ultramodern/config.hpp"       // GraphicsConfig + get/set_graphics_config
#include "librecomp/mods.hpp"           // recomp::mods::get_all_mod_details / enable_mod / is_mod_enabled

#include "ui_renderer.h"
#include "bar_ui.h"
#include "game/config.hpp"               // bar::config::save_graphics
#include "main/bar_cheats.h"             // bar_cheats — BAR cheat toggles

namespace fs = std::filesystem;
using ultramodern::renderer::GraphicsConfig;

// Defined in src/main/main.cpp — relaunches the executable (a fresh game) then quits this instance.
void bar_restart_game();

namespace {

// ----------------------------------------------------------------------------------------
// Module state. All RmlUi objects are created and used on the RT64 render/present thread (the
// init/draw hooks); the only cross-thread traffic is the SDL event queue and a few atomics.
// ----------------------------------------------------------------------------------------
SDL_Window* g_window = nullptr;
fs::path    g_default_rom;

recompui::RmlRenderInterface_RT64 g_render_interface;
std::unique_ptr<SystemInterface_SDL> g_system_interface;
Rml::Context* g_context = nullptr;
Rml::ElementDocument* g_launcher_doc = nullptr;
Rml::ElementDocument* g_config_doc = nullptr;
Rml::ElementDocument* g_cheats_doc = nullptr;
Rml::ElementDocument* g_pause_doc = nullptr;

// Which "root" menu is active, so a sub-page's Back returns to the right place: the launcher
// (pre-game) or the in-game pause menu. Touched only on the render thread (all show_*/Back run there).
enum class MenuRoot { Launcher, Pause };
MenuRoot g_active_root = MenuRoot::Launcher;

GraphicsConfig g_working_config;   // the config the menu edits; pushed to ultramodern on change
GraphicsConfig g_applied_config;   // snapshot of the config RT64 launched with — the restart-required baseline

std::atomic<bool> g_ui_ready{false};        // documents loaded; menu functional
std::atomic<bool> g_menu_open{false};       // launcher/pause/config/cheats currently shown + capturing input
std::atomic<bool> g_play_requested{false};  // user activated "Play"
std::atomic<bool> g_play_consumed{false};   // coordinator already took the request
std::atomic<bool> g_game_started{false};        // the recompiled game is running (enables the in-game pause menu)
std::atomic<bool> g_pause_toggle_pending{false};  // Esc/F1 pressed; the render thread should toggle the pause overlay

moodycamel::ConcurrentQueue<SDL_Event> g_event_queue;

// Event listeners must outlive the documents; keep them alive here.
std::vector<std::unique_ptr<Rml::EventListener>> g_listeners;

fs::path asset_path(const std::string& name) {
    // Assets ship next to the executable under assets/ui/ (copied there by CMake post-build).
    static const fs::path base = [] {
        char* p = SDL_GetBasePath();
        fs::path dir = (p != nullptr) ? fs::path(p) : fs::current_path();
        if (p != nullptr) SDL_free(p);
        return dir / "assets" / "ui";
    }();
    return base / name;
}

// Canonical enum<->string conversion, reusing the NLOHMANN_JSON_SERIALIZE_ENUM tables in
// config.hpp so the .rml <option value="..."> strings match exactly what bar::config persists.
template <typename E>
std::string enum_to_str(E value) {
    return nlohmann::json(value).template get<std::string>();
}
template <typename E>
E str_to_enum(const std::string& s, E fallback) {
    // The enum serializer maps an unknown string to the first table entry rather than throwing.
    E out = nlohmann::json(s).template get<E>();
    (void)fallback;
    return out;
}

// ----------------------------------------------------------------------------------------
// A trivial std::function-backed RmlUi event listener.
// ----------------------------------------------------------------------------------------
class FnListener final : public Rml::EventListener {
public:
    explicit FnListener(std::function<void(Rml::Event&)> fn) : fn_(std::move(fn)) {}
    void ProcessEvent(Rml::Event& event) override { fn_(event); }
private:
    std::function<void(Rml::Event&)> fn_;
};

void add_listener(Rml::Element* el, const Rml::String& type, std::function<void(Rml::Event&)> fn) {
    if (el == nullptr) return;
    auto listener = std::make_unique<FnListener>(std::move(fn));
    el->AddEventListener(type, listener.get());
    g_listeners.emplace_back(std::move(listener));
}

Rml::ElementFormControl* form_control(Rml::ElementDocument* doc, const char* id) {
    if (doc == nullptr) return nullptr;
    return rmlui_dynamic_cast<Rml::ElementFormControl*>(doc->GetElementById(id));
}

void set_control_value(const char* id, const std::string& value) {
    if (auto* c = form_control(g_config_doc, id)) {
        c->SetValue(value);
    }
}

// Guard so the programmatic SetValue calls in config_to_controls don't trip the delegated "change"
// handler into re-saving graphics.json mid-population — which clobbered not-yet-set controls (e.g. the
// MSAA-4x default got reset to None just by opening Settings, since msaa is populated after the first
// control whose SetValue fires the change).
bool g_populating_controls = false;

// Push g_working_config's current values into the config document's controls.
void config_to_controls() {
    g_populating_controls = true;
    set_control_value("rr_option",   enum_to_str(g_working_config.rr_option));
    set_control_value("rr_manual",   std::to_string(g_working_config.rr_manual_value));
    set_control_value("res_option",  enum_to_str(g_working_config.res_option));
    set_control_value("ds_option",   std::to_string(g_working_config.ds_option));   // supersampling (SSAA) factor
    set_control_value("wm_option",   enum_to_str(g_working_config.wm_option));
    set_control_value("msaa_option", enum_to_str(g_working_config.msaa_option));
    set_control_value("divot_option", enum_to_str(g_working_config.divot_option));
    set_control_value("ar_option",   enum_to_str(g_working_config.ar_option));
    set_control_value("hr_option",   enum_to_str(g_working_config.hr_option));
    set_control_value("hpfb_option", enum_to_str(g_working_config.hpfb_option));
    g_populating_controls = false;
}

std::string control_value(const char* id, const std::string& fallback) {
    if (auto* c = form_control(g_config_doc, id)) {
        return c->GetValue();
    }
    return fallback;
}

// Resolution scale, supersampling and MSAA change the render-target geometry/sample count, so they are
// only applied via RT64's validated setup path (see rt64_render_context.cpp) — i.e. on the next launch.
// A restart is "required" once any of them differs from the config RT64 actually started with.
bool restart_required() {
    return g_working_config.res_option  != g_applied_config.res_option
        || g_working_config.ds_option   != g_applied_config.ds_option
        || g_working_config.msaa_option != g_applied_config.msaa_option;
}

// Show/hide the "changes need a restart" bar in the config document to match restart_required().
void update_restart_banner() {
    if (auto* b = (g_config_doc != nullptr) ? g_config_doc->GetElementById("restart_bar") : nullptr) {
        b->SetClass("show", restart_required());
    }
}

// Read the controls back into g_working_config, apply to ultramodern, and persist.
void controls_to_config_and_apply() {
    if (g_populating_controls) return;   // ignore change events fired by programmatic population (see guard)
    using namespace ultramodern::renderer;
    g_working_config.rr_option   = str_to_enum(control_value("rr_option",   enum_to_str(g_working_config.rr_option)),   g_working_config.rr_option);
    g_working_config.res_option  = str_to_enum(control_value("res_option",  enum_to_str(g_working_config.res_option)),  g_working_config.res_option);
    g_working_config.wm_option   = str_to_enum(control_value("wm_option",   enum_to_str(g_working_config.wm_option)),   g_working_config.wm_option);
    g_working_config.msaa_option = str_to_enum(control_value("msaa_option", enum_to_str(g_working_config.msaa_option)), g_working_config.msaa_option);
    g_working_config.divot_option = str_to_enum(control_value("divot_option", enum_to_str(g_working_config.divot_option)), g_working_config.divot_option);
    g_working_config.ar_option   = str_to_enum(control_value("ar_option",   enum_to_str(g_working_config.ar_option)),   g_working_config.ar_option);
    g_working_config.hr_option   = str_to_enum(control_value("hr_option",   enum_to_str(g_working_config.hr_option)),   g_working_config.hr_option);
    g_working_config.hpfb_option = str_to_enum(control_value("hpfb_option", enum_to_str(g_working_config.hpfb_option)), g_working_config.hpfb_option);
    try {
        g_working_config.rr_manual_value = std::stoi(control_value("rr_manual", std::to_string(g_working_config.rr_manual_value)));
    } catch (...) { /* keep previous */ }
    try {
        g_working_config.ds_option = std::stoi(control_value("ds_option", std::to_string(g_working_config.ds_option)));
    } catch (...) { /* keep previous */ }

    ultramodern::renderer::set_graphics_config(g_working_config);
    bar::config::save_graphics(g_working_config);
    update_restart_banner();   // reflect whether a restart-required setting now differs from launch
}

// Build the mod list rows (best-effort; empty until the mod loader has scanned a mods/ folder).
void populate_mod_list() {
    Rml::Element* list = (g_launcher_doc != nullptr) ? g_launcher_doc->GetElementById("mod_list") : nullptr;
    if (list == nullptr) return;

    std::vector<recomp::mods::ModDetails> mods;
    try {
        mods = recomp::mods::get_all_mod_details("bar");
    } catch (...) {
        mods.clear();
    }

    if (mods.empty()) {
        list->SetInnerRML("<p class=\"mod-empty\">No mods installed. Drop .nrm files in the mods/ folder.</p>");
        return;
    }

    Rml::String rml;
    for (const auto& m : mods) {
        bool enabled = false;
        try { enabled = recomp::mods::is_mod_enabled(m.mod_id); } catch (...) {}
        rml += "<div class=\"mod-row\"><span class=\"mod-name\">";
        rml += m.display_name.empty() ? m.mod_id : m.display_name;
        rml += "</span><input type=\"checkbox\" class=\"mod-toggle\" data-mod-id=\"";
        rml += m.mod_id;
        rml += "\"";
        rml += enabled ? " checked" : "";
        rml += "/></div>";
    }
    list->SetInnerRML(rml);

    // One delegated change handler on the list container: a checkbox toggling fires "change", which
    // bubbles up to here. The toggled mod is identified by its data-mod-id attribute.
    add_listener(list, "change", [](Rml::Event& ev) {
        Rml::Element* target = ev.GetTargetElement();
        if (target == nullptr) return;
        const Rml::String mod_id = target->GetAttribute<Rml::String>("data-mod-id", Rml::String());
        if (mod_id.empty()) return;
        // RmlUi checkboxes report a non-empty value (the "value" attribute, default "on") when
        // checked and an empty value when unchecked.
        bool enabled = false;
        if (auto* cb = rmlui_dynamic_cast<Rml::ElementFormControl*>(target)) {
            enabled = !cb->GetValue().empty();
        }
        try { recomp::mods::enable_mod(mod_id, enabled); } catch (...) {}
    });
}

void hide_all_docs() {
    if (g_launcher_doc) g_launcher_doc->Hide();
    if (g_config_doc)   g_config_doc->Hide();
    if (g_cheats_doc)   g_cheats_doc->Hide();
    if (g_pause_doc)    g_pause_doc->Hide();
}

// Close the whole overlay and release input back to the game (draw_hook unpauses the sim once the
// menu is no longer open).
void close_overlay() {
    hide_all_docs();
    g_menu_open.store(false);
}

void show_launcher() {
    hide_all_docs();
    g_active_root = MenuRoot::Launcher;
    if (g_launcher_doc) {
        g_launcher_doc->Show();
        if (auto* e = g_launcher_doc->GetElementById("play")) e->Focus(true);   // keyboard: start on Play
    }
    g_menu_open.store(true);
}

void show_pause() {
    hide_all_docs();
    g_active_root = MenuRoot::Pause;
    if (g_pause_doc) {
        g_pause_doc->Show();
        if (auto* e = g_pause_doc->GetElementById("resume")) e->Focus(true);    // keyboard: start on Resume
    }
    g_menu_open.store(true);
}

void show_config() {
    hide_all_docs();
    if (g_config_doc) {
        config_to_controls();
        update_restart_banner();
        g_config_doc->Show();
        if (auto* e = g_config_doc->GetElementById("rr_option")) e->Focus(true);
    }
    g_menu_open.store(true);
}

void show_cheats() {
    hide_all_docs();
    if (g_cheats_doc) {
        g_cheats_doc->Show();
        Rml::Element* list = g_cheats_doc->GetElementById("cheat_list");
        Rml::Element* first = (list != nullptr && list->GetNumChildren() > 0) ? list->GetChild(0)
                                                                              : g_cheats_doc->GetElementById("back");
        if (first != nullptr) first->Focus(true);   // keyboard: start on the first cheat
    }
    g_menu_open.store(true);
}

// Return from a sub-page (Settings/Cheats) to whichever root opened it.
void go_back() {
    if (g_active_root == MenuRoot::Pause) show_pause();
    else                                  show_launcher();
}

void wire_launcher_events() {
    if (g_launcher_doc == nullptr) return;
    add_listener(g_launcher_doc->GetElementById("play"), "click", [](Rml::Event&) {
        // The game is already running behind the menu (RT64 only drives the menu's render+input hook
        // while the game produces frames, so the menu must overlay the live game). "Play" just hides
        // the overlay to reveal/enter the game; Esc/F1 bring up the pause menu.
        close_overlay();
    });
    add_listener(g_launcher_doc->GetElementById("settings"), "click", [](Rml::Event&) { show_config(); });
    add_listener(g_launcher_doc->GetElementById("cheats"), "click", [](Rml::Event&) { show_cheats(); });
    add_listener(g_launcher_doc->GetElementById("quit"), "click", [](Rml::Event&) { ultramodern::quit(); });
    populate_mod_list();
}

void wire_config_events() {
    if (g_config_doc == nullptr) return;
    add_listener(g_config_doc->GetElementById("back"), "click", [](Rml::Event&) { go_back(); });
    // "Restart now" (shown only when a restart-required setting changed): relaunch so the new
    // resolution / SSAA / MSAA apply through RT64's validated setup path. save_graphics() already
    // ran on the change, so graphics.json is current before the relaunch.
    add_listener(g_config_doc->GetElementById("apply_restart"), "click", [](Rml::Event&) { bar_restart_game(); });
    // One delegated change handler on the form: any control change re-reads + applies + saves.
    Rml::Element* form = g_config_doc->GetElementById("graphics_form");
    add_listener(form != nullptr ? form : g_config_doc->GetElementById("config_root"), "change",
                 [](Rml::Event&) { controls_to_config_and_apply(); });
}

// Build the cheat checkbox rows from bar_cheats' table (once), with a delegated change handler that
// maps each toggle back to its cheat id. Mirrors populate_mod_list().
void populate_cheats() {
    Rml::Element* list = (g_cheats_doc != nullptr) ? g_cheats_doc->GetElementById("cheat_list") : nullptr;
    if (list == nullptr) return;

    Rml::String rml;
    for (int i = 0; i < (int)bar_cheats::Id::Count; i++) {
        const bar_cheats::Info& inf = bar_cheats::info((bar_cheats::Id)i);
        const bool enabled = bar_cheats::get((bar_cheats::Id)i);
        rml += "<div class=\"cheat-row";
        rml += enabled ? " on" : "";
        rml += "\" data-cheat-id=\"";
        rml += inf.id;
        rml += "\"><span class=\"cheat-check\"><span class=\"tick\"></span></span><span class=\"cheat-name\">";
        rml += inf.label;
        rml += "</span><span class=\"cheat-sub\">";
        rml += inf.hint;
        rml += "</span></div>";
    }
    list->SetInnerRML(rml);

    // Delegated click handler: a click anywhere in a row toggles that cheat. The target may be a
    // child span, so walk up to the ancestor carrying data-cheat-id.
    add_listener(list, "click", [](Rml::Event& ev) {
        Rml::Element* el = ev.GetTargetElement();
        while (el != nullptr && el->GetAttribute<Rml::String>("data-cheat-id", Rml::String()).empty()) {
            el = el->GetParentNode();
        }
        if (el == nullptr) return;
        const Rml::String cheat_id = el->GetAttribute<Rml::String>("data-cheat-id", Rml::String());
        for (int i = 0; i < (int)bar_cheats::Id::Count; i++) {
            if (cheat_id == bar_cheats::info((bar_cheats::Id)i).id) {
                const bool now = !bar_cheats::get((bar_cheats::Id)i);
                bar_cheats::set((bar_cheats::Id)i, now);
                bar_cheats::save();
                el->SetClass("on", now);
                break;
            }
        }
    });
}

void wire_cheats_events() {
    if (g_cheats_doc == nullptr) return;
    add_listener(g_cheats_doc->GetElementById("back"), "click", [](Rml::Event&) { go_back(); });
    populate_cheats();
}

void wire_pause_events() {
    if (g_pause_doc == nullptr) return;
    add_listener(g_pause_doc->GetElementById("resume"),   "click", [](Rml::Event&) { close_overlay(); });
    add_listener(g_pause_doc->GetElementById("restart"),  "click", [](Rml::Event&) { bar_restart_game(); });
    add_listener(g_pause_doc->GetElementById("settings"), "click", [](Rml::Event&) { show_config(); });
    add_listener(g_pause_doc->GetElementById("cheats"),   "click", [](Rml::Event&) { show_cheats(); });
    add_listener(g_pause_doc->GetElementById("quit"),     "click", [](Rml::Event&) { ultramodern::quit(); });
}

// ----------------------------------------------------------------------------------------
// RT64 render hooks.
// ----------------------------------------------------------------------------------------
void init_hook(plume::RenderInterface* interface, plume::RenderDevice* device) {
#if defined(__linux__)
    std::locale::global(std::locale::classic());
#endif
    if (g_window == nullptr) return;

    g_working_config = ultramodern::renderer::get_graphics_config();
    g_applied_config = g_working_config;   // baseline: what RT64 is about to set up with (restart-required diff is vs this)

    g_system_interface = std::make_unique<SystemInterface_SDL>(g_window);
    g_render_interface.init(interface, device);

    Rml::SetSystemInterface(g_system_interface.get());
    Rml::SetRenderInterface(g_render_interface.get_rml_interface());
    Rml::Initialise();

    int width = 960, height = 720;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);
    g_context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (g_context == nullptr) {
        std::fprintf(stderr, "[BeetleRecomp][ui] failed to create RmlUi context\n");
        return;
    }
    Rml::Debugger::Initialise(g_context);

    // Fonts (single face is enough for a functional menu; add more for styling later).
    const char* fonts[] = { "LatoLatin-Regular.ttf" };
    for (const char* f : fonts) {
        const fs::path p = asset_path(f);
        if (!Rml::LoadFontFace(p.string(), false)) {
            std::fprintf(stderr, "[BeetleRecomp][ui] failed to load font %s\n", p.string().c_str());
        }
    }

    g_launcher_doc = g_context->LoadDocument(asset_path("launcher.rml").string());
    g_config_doc   = g_context->LoadDocument(asset_path("config.rml").string());
    g_cheats_doc   = g_context->LoadDocument(asset_path("cheats.rml").string());
    g_pause_doc    = g_context->LoadDocument(asset_path("pause.rml").string());
    if (g_launcher_doc == nullptr) {
        std::fprintf(stderr, "[BeetleRecomp][ui] failed to load launcher.rml; menu disabled\n");
        return;
    }
    if (g_config_doc == nullptr) std::fprintf(stderr, "[BeetleRecomp][ui] warning: config.rml not loaded\n");
    if (g_cheats_doc == nullptr) std::fprintf(stderr, "[BeetleRecomp][ui] warning: cheats.rml not loaded\n");
    if (g_pause_doc  == nullptr) std::fprintf(stderr, "[BeetleRecomp][ui] warning: pause.rml not loaded\n");

    wire_launcher_events();
    wire_config_events();
    wire_cheats_events();
    wire_pause_events();
    // Initial overlay page. BAR_SKIP_LAUNCHER starts with the menu hidden (headless / BAR_FORCESTATE
    // runs); BAR_UI_PAGE=cheats|settings opens straight to that page (UI dev aid).
    const char* start_page = std::getenv("BAR_UI_PAGE");
    if (std::getenv("BAR_SKIP_LAUNCHER") != nullptr) {
        g_menu_open.store(false);
        if (g_launcher_doc) g_launcher_doc->Hide();
        if (g_config_doc) g_config_doc->Hide();
        if (g_cheats_doc) g_cheats_doc->Hide();
    }
    else if (start_page != nullptr && std::string(start_page) == "cheats")   show_cheats();
    else if (start_page != nullptr && std::string(start_page) == "settings") show_config();
    else                                                                     show_launcher();

    g_ui_ready.store(true);
    std::fprintf(stderr, "[BeetleRecomp][ui] launcher ready\n");
}

void draw_hook(plume::RenderCommandList* command_list, plume::RenderFramebuffer* swap_chain_framebuffer) {
    if (!g_ui_ready.load() || g_context == nullptr) {
        return;
    }

    // Esc/F1 (main thread) requested an in-game pause-menu toggle; perform the document show/hide here
    // on the render thread, where every other RmlUi document mutation happens. Open -> close (resume);
    // closed -> show the pause menu.
    if (g_pause_toggle_pending.exchange(false) && g_game_started.load()) {
        if (g_menu_open.load()) close_overlay();
        else                    show_pause();
    }

    // Drain queued SDL input into the context (only while the menu owns input). RmlUi handles Tab
    // (tab-index) and arrow keys (the `nav` RCSS property) for focus movement itself; we add Enter /
    // Space to activate the focused element (our buttons/rows are <div>s, which RmlUi doesn't auto-click).
    SDL_Event ev;
    while (g_event_queue.try_dequeue(ev)) {
        if (g_menu_open.load()) {
            RmlSDL::InputEventHandler(g_context, g_window, ev);
            // Activate the focused element on Enter / Space (our buttons/rows are <div>s; RmlUi handles
            // Tab + arrow-key focus movement itself via tab-index / the `nav` property, but not activation).
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER || ev.key.keysym.sym == SDLK_SPACE)) {
                if (Rml::Element* focused = g_context->GetFocusElement()) focused->Click();
            }
        }
    }

    // Emulator-style pause: freeze the simulation while the in-game pause menu (or a sub-page from it)
    // is up. NOT while the boot-time launcher overlay is shown (pausing during boot wedges the game).
    // The render context re-renders the last frame while paused so this hook keeps running.
    ultramodern::set_paused(g_game_started.load() && g_menu_open.load() && g_active_root == MenuRoot::Pause);

    if (!g_menu_open.load()) {
        return;   // game is running and the menu is hidden: draw nothing over the frame
    }

    int width = 960, height = 720;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);

    g_render_interface.start(command_list, width, height);
    g_context->SetDimensions(Rml::Vector2i(width, height));
    g_context->Update();
    g_context->Render();
    g_render_interface.end(command_list, swap_chain_framebuffer);
}

void deinit_hook() {
    g_ui_ready.store(false);
    g_menu_open.store(false);
    g_launcher_doc = nullptr;
    g_config_doc = nullptr;
    g_cheats_doc = nullptr;
    g_pause_doc = nullptr;
    g_context = nullptr;
    g_listeners.clear();
    Rml::Shutdown();
    g_render_interface.reset();
}

} // namespace

namespace bar_ui {

void install(SDL_Window* window, const fs::path& default_rom) {
    g_window = window;
    g_default_rom = default_rom;
    RT64::SetRenderHooks(init_hook, draw_hook, deinit_hook);
}

void handle_sdl_event(const SDL_Event& event) {
    g_event_queue.enqueue(event);
}

bool menu_capturing_input() {
    return g_menu_open.load();
}

bool consume_play_request(fs::path& out_rom) {
    if (g_play_requested.load() && !g_play_consumed.exchange(true)) {
        out_rom = g_default_rom;
        return true;
    }
    return false;
}

void on_game_started() {
    // The game is now producing frames, so the menu overlay is live + interactive. Keep the launcher
    // shown over the (booting) game until the user clicks Play; just record that the game has started
    // (this enables the F1 in-game toggle).
    g_game_started.store(true);
}

void toggle_pause_menu() {
    // Bound to Esc / F1 / controller Back. Only meaningful once the game is running (pre-game the
    // launcher is already shown). The render thread (draw_hook) performs the actual show/hide — and the
    // pause/resume of the simulation — so RmlUi documents are only touched there.
    if (!g_game_started.load() || !g_ui_ready.load()) return;
    g_pause_toggle_pending.store(true);
}

} // namespace bar_ui
