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

namespace fs = std::filesystem;
using ultramodern::renderer::GraphicsConfig;

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

GraphicsConfig g_working_config;   // the config the menu edits; pushed to ultramodern on change

std::atomic<bool> g_ui_ready{false};        // documents loaded; menu functional
std::atomic<bool> g_menu_open{false};       // launcher/config currently shown + capturing input
std::atomic<bool> g_play_requested{false};  // user activated "Play"
std::atomic<bool> g_play_consumed{false};   // coordinator already took the request

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

// Push g_working_config's current values into the config document's controls.
void config_to_controls() {
    set_control_value("rr_option",   enum_to_str(g_working_config.rr_option));
    set_control_value("rr_manual",   std::to_string(g_working_config.rr_manual_value));
    set_control_value("res_option",  enum_to_str(g_working_config.res_option));
    set_control_value("wm_option",   enum_to_str(g_working_config.wm_option));
    set_control_value("msaa_option", enum_to_str(g_working_config.msaa_option));
    set_control_value("ar_option",   enum_to_str(g_working_config.ar_option));
    set_control_value("hr_option",   enum_to_str(g_working_config.hr_option));
    set_control_value("hpfb_option", enum_to_str(g_working_config.hpfb_option));
}

std::string control_value(const char* id, const std::string& fallback) {
    if (auto* c = form_control(g_config_doc, id)) {
        return c->GetValue();
    }
    return fallback;
}

// Read the controls back into g_working_config, apply to ultramodern, and persist.
void controls_to_config_and_apply() {
    using namespace ultramodern::renderer;
    g_working_config.rr_option   = str_to_enum(control_value("rr_option",   enum_to_str(g_working_config.rr_option)),   g_working_config.rr_option);
    g_working_config.res_option  = str_to_enum(control_value("res_option",  enum_to_str(g_working_config.res_option)),  g_working_config.res_option);
    g_working_config.wm_option   = str_to_enum(control_value("wm_option",   enum_to_str(g_working_config.wm_option)),   g_working_config.wm_option);
    g_working_config.msaa_option = str_to_enum(control_value("msaa_option", enum_to_str(g_working_config.msaa_option)), g_working_config.msaa_option);
    g_working_config.ar_option   = str_to_enum(control_value("ar_option",   enum_to_str(g_working_config.ar_option)),   g_working_config.ar_option);
    g_working_config.hr_option   = str_to_enum(control_value("hr_option",   enum_to_str(g_working_config.hr_option)),   g_working_config.hr_option);
    g_working_config.hpfb_option = str_to_enum(control_value("hpfb_option", enum_to_str(g_working_config.hpfb_option)), g_working_config.hpfb_option);
    try {
        g_working_config.rr_manual_value = std::stoi(control_value("rr_manual", std::to_string(g_working_config.rr_manual_value)));
    } catch (...) { /* keep previous */ }

    ultramodern::renderer::set_graphics_config(g_working_config);
    bar::config::save_graphics(g_working_config);
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

void show_launcher() {
    if (g_config_doc) g_config_doc->Hide();
    if (g_launcher_doc) g_launcher_doc->Show();
    g_menu_open.store(true);
}

void show_config() {
    if (g_launcher_doc) g_launcher_doc->Hide();
    if (g_config_doc) {
        config_to_controls();
        g_config_doc->Show();
    }
    g_menu_open.store(true);
}

void wire_launcher_events() {
    if (g_launcher_doc == nullptr) return;
    add_listener(g_launcher_doc->GetElementById("play"), "click", [](Rml::Event&) {
        g_play_requested.store(true);
        if (auto* btn = (g_launcher_doc ? g_launcher_doc->GetElementById("play") : nullptr)) {
            btn->SetInnerRML("Starting...");
            btn->SetClass("disabled", true);
        }
    });
    add_listener(g_launcher_doc->GetElementById("settings"), "click", [](Rml::Event&) { show_config(); });
    add_listener(g_launcher_doc->GetElementById("quit"), "click", [](Rml::Event&) { ultramodern::quit(); });
    populate_mod_list();
}

void wire_config_events() {
    if (g_config_doc == nullptr) return;
    add_listener(g_config_doc->GetElementById("back"), "click", [](Rml::Event&) { show_launcher(); });
    // One delegated change handler on the form: any control change re-reads + applies + saves.
    Rml::Element* form = g_config_doc->GetElementById("graphics_form");
    add_listener(form != nullptr ? form : g_config_doc->GetElementById("config_root"), "change",
                 [](Rml::Event&) { controls_to_config_and_apply(); });
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
    if (g_launcher_doc == nullptr) {
        std::fprintf(stderr, "[BeetleRecomp][ui] failed to load launcher.rml; menu disabled\n");
        return;
    }

    wire_launcher_events();
    wire_config_events();
    show_launcher();

    g_ui_ready.store(true);
    std::fprintf(stderr, "[BeetleRecomp][ui] launcher ready\n");
}

void draw_hook(plume::RenderCommandList* command_list, plume::RenderFramebuffer* swap_chain_framebuffer) {
    if (!g_ui_ready.load() || g_context == nullptr) {
        return;
    }

    // Drain queued SDL input into the context (only while the menu owns input).
    SDL_Event ev;
    while (g_event_queue.try_dequeue(ev)) {
        if (g_menu_open.load()) {
            RmlSDL::InputEventHandler(g_context, g_window, ev);
        }
    }

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
    g_menu_open.store(false);
    if (g_launcher_doc) g_launcher_doc->Hide();
    if (g_config_doc) g_config_doc->Hide();
}

} // namespace bar_ui
