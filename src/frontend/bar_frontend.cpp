#include "frontend/bar_frontend.h"
#include "main/bar_rumble.hpp"
#include "main/bar_cheats.h"
#include "main/bar_input.hpp"
#include "game/input_config.hpp"
#include "game/config.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>

#include "recompui/recompui.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "recompui/config.h"
#include "elements/ui_svg.h"
#include "recompinput/input_events.h"
#include "recompinput/input_mapping.h"
#include "recompinput/input_state.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "librecomp/game.hpp"
#include "ultramodern/config.hpp"
#include <filesystem>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

namespace {

// Must match the recomp::GameEntry registered in main.cpp — recompui drives the launcher's
// "Load ROM" / "Start Game" state from librecomp's validation of this id, which is in turn backed by
// the entry's XXH3-64 rom_hash. A mismatch here silently yields a launcher that can never start.
constexpr const char8_t* kGameId      = u8"bar.n64.us";
constexpr const char*    kModGameId   = "bar";
constexpr const char*    kDisplayName = "Beetle Adventure Racing!";

// How many controller ports this port serves. Beetle Adventure Racing does four-player split-screen,
// so it is the console's four -- which is also recompinput's own maximum.
//
// It is the single place that decides it: the Controls tab's player slots, which pads
// refresh_players will assign, and the ports poll_port and port_assigned answer for all come from
// here.
constexpr int kMaxPlayers = 4;
std::array<std::atomic<uint64_t>, kMaxPlayers> g_port_samples{};
std::array<std::atomic<bool>, kMaxPlayers> g_raw_rumble{};
std::atomic<bool> g_menu_capturing{};

// Assets are resolved as <exe dir>/assets/<name> (recompui::file::get_asset_path). The primary font
// is REQUIRED: recompui throws "No primary font was registered" without it. The family name is what
// the stylesheet must reference, and is not necessarily the filename.
constexpr const char* kPrimaryFontFile   = "LatoLatin-Regular.ttf";
constexpr const char* kPrimaryFontFamily = "LatoLatin";   // the font file's real family name

// TEMPORARY bring-up tracing. The frontend build currently exits during renderer/UI setup with no
// diagnostic, so these mark how far the sequence gets. Remove once it starts cleanly.
void trace(const char* msg) {
    std::fprintf(stderr, "[frontend] %s\n", msg);
    std::fflush(stderr);
}

} // namespace

// Implemented in src/main/bar_frustum.cpp, which owns the frustum the game draws and culls with.
extern "C" void bar_set_draw_distance(float scale);

// Implemented in src/main/main.cpp, which owns the SDL audio device the finished samples go to.
// The Sound tab's Main Volume used to be inert: recompui defines the slider and reads it back, and
// nothing upstream ever applies it -- the port is expected to, and this one did not.
extern "C" void bar_set_audio_volume(double percent);
extern "C" void bar_set_mute_when_unfocused(bool mute);
extern "C" bool bar_output_silenced(void);
extern bool g_bar_had_legacy_input_config;

// Draw Distance — a BAR-specific option appended to recompui's prefab Graphics tab.
//
// It is added from here rather than from RecompFrontend because that submodule is upstream
// (N64Recomp/RecompFrontend) and this port cannot push to it; the tab's Config is returned by
// create_graphics_tab(), so an extra option can simply be added to it. It has to happen BEFORE
// recompui::config::finalize(), which is what loads the JSON — an option added afterwards would
// never see its saved value.
//
// The value reaches the renderer through bar_set_draw_distance (src/main/bar_frustum.cpp), which
// multiplies the far plane of BAR's own view frustum — and, because the game derives its culling
// planes from the same numbers, the distance it is willing to submit geometry for.
namespace {

// Keys are what land in graphics.json, so they must stay stable; the third field is the label.
// The keys are what land in graphics.json and must stay stable, but the config stores the KEY rather
// than the index, so 1.5x can be inserted in its natural place without invalidating a saved "2x"/"4x".
const std::vector<recomp::config::ConfigOptionEnumOption> kDrawDistanceOptions = {
    { 0u, "1x", "1x" },
    { 1u, "1.5x", "1.5x" },
    { 2u, "2x", "2x" },
    { 3u, "4x", "4x" },
};

const std::string kDrawDistanceOption = "draw_distance";

float draw_distance_scale_for(uint32_t index) {
    switch (index) {
        case 0:  return 1.0f;
        case 1:  return 1.5f;
        case 2:  return 2.0f;
        default: return 4.0f;
    }
}

void add_draw_distance_option() {
    recomp::config::Config &config = recompui::config::get_config(recompui::config::graphics::id);
    config.add_enum_option(
        kDrawDistanceOption,
        "Draw Distance",
        // Plain prose, no markup beyond <recomp-color>: this description is parsed as RML, and a bare
        // <br> is not self-closing there. It produced "closing tag 'body' mismatched, was expecting
        // 'br'" in the frontend's event log -- the document still rendered, so the only symptom was a
        // logged error. Every other option description here is plain text; keep it that way.
        "How far into the distance the game draws. The original hardware drew 300 units ahead; the "
        "higher settings extend that, and extend the distance the game submits scenery for to match, "
        "so it stops appearing out of the fog. "
        "<recomp-color primary>1.5x</recomp-color> is the highest setting verified not to drop "
        "objects; above it, courses with long views can make cars and trees disappear for a moment, "
        "and <recomp-color primary>Wicked Woods</recomp-color> is automatically limited to 1.5x "
        "whatever you pick here.",
        kDrawDistanceOptions,
        // 1.5x: the highest multiplier bisected as safe on the worst case found so far (Wicked Woods'
        // big jump), and the extra distance is visible. 1.75x drops the player's car on the approach
        // to landing there; 2x and 4x drop it for most of the jump. The cap is enforced per course in
        // src/main/bar_frustum.cpp, so 2x and 4x stay selectable and stay honest everywhere else.
        0u /* Original distance: larger values remain opt-in. */
    );
}

// Read the applied (not pending) value and hand it to the renderer. Called after finalize() and once
// per frame from pump_events, rather than through set_save_callback, because the Graphics tab has
// already installed its own save callback and replacing it would stop every other graphics setting
// applying. A map lookup per frame is far cheaper than that bug.
void push_draw_distance() {

    const recomp::config::ConfigValueVariant value =
        recompui::config::get_config(recompui::config::graphics::id).get_option_value(kDrawDistanceOption);
    if (const uint32_t *index = std::get_if<uint32_t>(&value)) {
        bar_set_draw_distance(draw_distance_scale_for(*index));
    }
}

} // namespace

namespace {
// Preserve existing input.json bindings when creating the frontend's controls.json.
// Retain input.json itself so a migration can always be recovered or inspected.
void import_legacy_bindings() {
    using recompinput::GameInput;
    using recompinput::InputDevice;
    using recompinput::InputField;
    using bar::input_config::BindSource;
    constexpr GameInput inputs[] = {
        GameInput::A, GameInput::B, GameInput::Z, GameInput::START, GameInput::L, GameInput::R,
        GameInput::C_UP, GameInput::C_DOWN, GameInput::C_LEFT, GameInput::C_RIGHT,
        GameInput::DPAD_UP, GameInput::DPAD_DOWN, GameInput::DPAD_LEFT, GameInput::DPAD_RIGHT,
        GameInput::Y_AXIS_POS, GameInput::Y_AXIS_NEG, GameInput::X_AXIS_NEG, GameInput::X_AXIS_POS,
    };
    static_assert(std::size(inputs) == static_cast<size_t>(bar::input_config::N64Input::Count));
    const auto copy_bindings = [&](int profile, InputDevice device, const bar::input_config::Bindings& bindings) {
        for (size_t i = 0; i < std::size(inputs); ++i) {
            const auto& old = bindings.map[i];
            InputField field{};
            if (device == InputDevice::Keyboard && old.source == BindSource::Keyboard &&
                old.code > 0 && old.code < SDL_NUM_SCANCODES) {
                field = InputField::keyboard(static_cast<SDL_Scancode>(old.code));
            } else if (device == InputDevice::Controller && old.source == BindSource::GamepadButton &&
                       old.code >= 0 && old.code < SDL_CONTROLLER_BUTTON_MAX) {
                field = InputField::controller_digital(static_cast<SDL_GameControllerButton>(old.code));
            } else if (device == InputDevice::Controller && old.source == BindSource::GamepadAxis &&
                       old.code >= 0 && old.code < SDL_CONTROLLER_AXIS_MAX) {
                field = InputField::controller_analog(static_cast<SDL_GameControllerAxis>(old.code), old.dir > 0);
            }
            recompinput::profiles::clear_input_binding(profile, inputs[i]);
            recompinput::profiles::set_input_binding(profile, inputs[i], 0, field);
        }
    };
    const auto legacy = bar::input_config::get();
    for (int port = 0; port < kMaxPlayers; ++port) {
        for (const auto device : {InputDevice::Keyboard, InputDevice::Controller}) {
            const auto type = device == InputDevice::Keyboard
                ? bar::input_config::DeviceType::Keyboard : bar::input_config::DeviceType::Gamepad;
            const auto& bindings = legacy.ports[port].bindings;
            if (!bar::input_config::bindings_match_device(bindings, type)) continue;
            const int profile = recompinput::profiles::add_input_profile(
                    "legacy_port_" + std::to_string(port) + (device == InputDevice::Keyboard ? "_keyboard" : "_controller"),
                    "Imported Player " + std::to_string(port + 1), device, true);
            copy_bindings(profile, device, bindings);
            if (port == 0) {
                copy_bindings(device == InputDevice::Keyboard ? recompinput::profiles::get_sp_keyboard_profile_index()
                                                              : recompinput::profiles::get_sp_controller_profile_index(), device, bindings);
            }
            if (device == InputDevice::Controller && !legacy.ports[port].device.guid.empty()) {
                const auto& ref = legacy.ports[port].device;
                const auto sdl_guid = SDL_JoystickGetGUIDFromString(ref.guid.c_str());
                recompinput::ControllerGUID guid{};
                Uint16 vendor{}, product{}, version{}, crc16{};
                SDL_GetJoystickGUIDInfo(sdl_guid, &vendor, &product, &version, &crc16);
                guid.vendor = vendor;
                guid.product = product;
                guid.version = version;
                guid.crc16 = crc16;
                const auto separator = ref.uid.find('!');
                if (separator != std::string::npos) guid.serial = ref.uid.substr(separator + 1);
                recompinput::profiles::add_controller(guid, profile);
            }
        }
    }
    // Named presets remain available even when they were not applied to a port.
    for (size_t i = 0; i < legacy.profiles.size(); ++i) {
        const auto& preset = legacy.profiles[i];
        for (const auto device : {InputDevice::Keyboard, InputDevice::Controller}) {
            const auto type = device == InputDevice::Keyboard ? bar::input_config::DeviceType::Keyboard
                                                            : bar::input_config::DeviceType::Gamepad;
            if (!bar::input_config::bindings_match_device(preset.bindings, type)) continue;
            const int profile = recompinput::profiles::add_input_profile(
                "legacy_preset_" + std::to_string(i) + (device == InputDevice::Keyboard ? "_keyboard" : "_controller"),
                preset.name, device, true);
            copy_bindings(profile, device, preset.bindings);
        }
    }
    if (!recompinput::profiles::save_controls_config(recomp::get_config_path() / "controls.json")) {
        std::fprintf(stderr, "[BeetleRecomp] Could not persist imported input bindings; input.json remains intact.\n");
    }
}
} // namespace

void bar::frontend::install() {
    // BAR_DBG_UI=1 sends the frontend's diagnostics to bar_ui_trace.log in the working directory.
    // Worth keeping: this build links as /SUBSYSTEM:WINDOWS, so it has no console and shell
    // redirection of stderr captures nothing — without this, frontend problems are silent.
    if (std::getenv("BAR_DBG_UI") != nullptr) {
        static std::FILE* log = std::freopen("bar_ui_trace.log", "w", stderr);
        (void)log;
    }

    trace("install: begin");

    recompui::programconfig::set_program_name(kDisplayName);
    recompui::programconfig::set_program_id(kGameId);

    recompui::register_primary_font(kPrimaryFontFile, kPrimaryFontFamily);

    // Built when recompui creates its menus. The library owns the ROM flow entirely:
    // add_start_game_or_load_rom_option() shows "Load ROM" until librecomp reports a valid ROM for
    // kGameId and "Start Game" afterwards, with a native file picker behind it — so ROM loading and
    // hash verification need no code on this side.
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        trace("launcher init callback: begin");

        recompui::GameOptionsMenu* options = menu->init_game_options_menu(
            kGameId, kModGameId, kDisplayName,
            // No thumbnail: any artwork for the game itself would be taken from the cartridge, and
            // this project ships nothing derived from a dump. The launcher lays out fine without one.
            {},
            recompui::GameOptionsMenuLayout::Center);

        // The launcher's background, which is a different thing from the game thumbnail above: this
        // is the project's OWN icon (icons/Icon.png, wrapped into an SVG by
        // scripts/make-launcher-logo.py), so it carries none of the cartridge's restrictions.
        //
        // The square project icon belongs above the title. Size it from the window height;
        // a full-width background centered at 50% overlaps the five menu options.
        auto* logo = menu->set_launcher_background_svg("icons/Logo.svg");
        logo->set_top(10.0f, recompui::Unit::Percent);
        logo->set_left(50.0f, recompui::Unit::Percent);
        logo->set_height(12.0f, recompui::Unit::Percent);
        logo->set_width_auto();
        logo->set_translate_2D(-50.0f, -50.0f, recompui::Unit::Percent);

        options->add_start_game_or_load_rom_option();
        options->add_setup_controls_option();
        options->add_settings_option();
        // Mods. The runtime half has worked all along -- recomp::start() calls initialize_mods() and
        // scan_mods() itself, and main.cpp gives librecomp this game's mod id -- but with no entry
        // to the tab, a mod could be installed and never appear, never be enabled, and never be
        // reported broken. The tab lists what was found; this opens it without starting the game.
        options->add_mods_option();
        // "Quit" rather than the library's "Exit", matching this project's reference port. It calls
        // ultramodern::quit(), which unwinds the game thread and the renderer in order rather than
        // tearing the process down, so a race in progress still writes its Controller Pak records.
        options->add_exit_option("Quit");

        trace("launcher init callback: menu built");
    });

    // The keyboard scheme, declared as the frontend's DEFAULTS rather than left to RecompFrontend,
    // whose own defaults are a different scheme again (WASD and space).
    //
    // Defaults preserve this port's historical keyboard scheme.
    // B is Z, the Z trigger is Left Shift, and L/R are Q/W. Saved profiles keep
    // their own bindings.
    //
    // Defaults apply to a profile the first time it is created; a keyboard profile already saved in
    // controls.json keeps whatever it holds until it is reset in the Controls tab.
    {
        using recompinput::GameInput;
        using recompinput::InputField;
        const struct { GameInput input; SDL_Scancode key; } keys[] = {
            { GameInput::X_AXIS_NEG,  SDL_SCANCODE_LEFT },
            { GameInput::X_AXIS_POS,  SDL_SCANCODE_RIGHT },
            { GameInput::Y_AXIS_POS,  SDL_SCANCODE_UP },
            { GameInput::Y_AXIS_NEG,  SDL_SCANCODE_DOWN },
            { GameInput::A,           SDL_SCANCODE_X },
            { GameInput::B,           SDL_SCANCODE_Z },
            { GameInput::Z,           SDL_SCANCODE_LSHIFT },
            { GameInput::START,       SDL_SCANCODE_RETURN },
            { GameInput::L,           SDL_SCANCODE_Q },
            { GameInput::R,           SDL_SCANCODE_W },
            // The C buttons work the camera, which BAR uses constantly, so they stay under the
            // right hand while the left drives.
            { GameInput::C_UP,        SDL_SCANCODE_I },
            { GameInput::C_DOWN,      SDL_SCANCODE_K },
            { GameInput::C_LEFT,      SDL_SCANCODE_J },
            { GameInput::C_RIGHT,     SDL_SCANCODE_L },
            { GameInput::DPAD_UP,     SDL_SCANCODE_T },
            { GameInput::DPAD_DOWN,   SDL_SCANCODE_G },
            { GameInput::DPAD_LEFT,   SDL_SCANCODE_F },
            { GameInput::DPAD_RIGHT,  SDL_SCANCODE_H },
        };
        for (const auto& binding : keys) {
            recompinput::set_default_mapping_for_keyboard(
                binding.input, { InputField::keyboard(binding.key) });
        }
    }

    // Four player slots in the Controls tab, because the game has four. This is not a narrowing --
    // it is what stops the tab assuming a single player.
    //
    // Which pad is which is then not a choice anyone should have to make before their first race:
    // pads are assigned in the order they are connected, so the first is player one and the second
    // is player two (see refresh_players in pump_events below), and the tab's "Assign players" modal
    // is left for anyone who wants to override that.
    recompinput::players::set_player_count_range(1, kMaxPlayers);

    // Config tabs. recompui's create_menus() calls config::init_modal(), which throws
    // "Configurations have not been loaded. Call recompui::config::finalize() first." unless the
    // tabs exist and finalize() has run — so this must happen here, before recomp::start() brings
    // the renderer up. Each tab persists to its own <id>.json in the app config directory.
    //
    // Rumble strength is enabled because this port serves the rumble motor register alongside the
    // Controller Pak, so a pad really can vibrate; gyro and mouse are off, since BAR has no use for
    // either.
    recompui::config::create_general_tab(recompui::config::GeneralTabOptions{
        .has_rumble_strength  = true,
        .has_gyro_sensitivity = false,
        .has_mouse_sensitivity = false,
    });
    auto& graphics = recompui::config::create_graphics_tab();
    using ultramodern::renderer::DivotFilter;
    using ultramodern::renderer::PresentFillMode;
    graphics.add_enum_option("divot_option", "VI Divot Filter",
        "Applies the N64 video filter that reduces small seams in the picture.",
        {{DivotFilter::Auto, "Auto"}, {DivotFilter::On, "On"}, {DivotFilter::Off, "Off"}},
        static_cast<uint32_t>(ultramodern::renderer::get_graphics_config().divot_option));
    graphics.add_enum_option("present_fill_mode", "Window Fit",
        "Pillarbox preserves the whole picture. Crop fills the window by trimming it. Stretch fills the window by changing its proportions.",
        {{PresentFillMode::Crop, "Crop"}, {PresentFillMode::Stretch, "Stretch"}, {PresentFillMode::Pillarbox, "Pillarbox"}},
        static_cast<uint32_t>(ultramodern::renderer::get_graphics_config().pfm_option));
    for (const char* option : {"divot_option", "present_fill_mode"}) {
        graphics.add_option_change_callback(option,
            [key = std::string(option)](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
                                       recomp::config::OptionChangeContext context) {
                if (context == recomp::config::OptionChangeContext::Temporary) return;
                if (const auto* index = std::get_if<uint32_t>(&value)) {
                    auto config = ultramodern::renderer::get_graphics_config();
                    if (key == "divot_option") config.divot_option = static_cast<DivotFilter>(*index);
                    else config.pfm_option = static_cast<PresentFillMode>(*index);
                    ultramodern::renderer::set_graphics_config(config);
                }
            });
    }
    graphics.on_json_parse_option(recompui::config::graphics::options::hr_option,
        [](const nlohmann::json& value) -> recomp::config::ConfigValueVariant {
            using ultramodern::renderer::HUDRatioMode;
            if (value.is_string()) {
                const auto name = value.get<std::string>();
                if (name == "Full" || name == "Expand") return uint32_t(HUDRatioMode::Full);
                if (name == "Clamp16x9" || name == "16:9") return uint32_t(HUDRatioMode::Clamp16x9);
                if (name == "Original") return uint32_t(HUDRatioMode::Original);
            }
            return uint32_t(bar::config::default_graphics_config().hr_option);
        });
    add_draw_distance_option();

    // Sound. Main Volume did nothing until this. recompui defines the slider and reads it back, and
    // nothing upstream ever applies it -- the port is expected to, and this one was not, so the
    // control was inert in every build so far. The callback covers all three ways the value
    // changes: Load, when the saved setting is read at startup; Temporary, while the slider is being
    // dragged, which is what makes it audible as you move it; and Permanent, on Apply.
    recomp::config::Config& sound = recompui::config::create_sound_tab();
    sound.add_option_change_callback(
        recompui::config::sound::options::main_volume,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&value)) {
                bar_set_audio_volume(*percent);
            }
        });
    sound.add_bool_option(
        "mute_unfocused", "Mute When Not In Focus",
        "Silences the game while another window has focus.",
        true);
    sound.add_option_change_callback(
        "mute_unfocused",
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const bool* mute = std::get_if<bool>(&value)) {
                bar_set_mute_when_unfocused(*mute);
            }
        });
    // There is no Music Volume beside those two, and the reason belongs here rather than in a
    // ticket: Main Volume is applied to the finished buffer, which is the only place this port can
    // reach. Music and effects are already mixed together by the time the samples arrive, so
    // separating them means reaching into the game's own sequence players -- work that has not been
    // done for BAR. wave-race-64-recomp has the option because that work was done there.

    recompui::config::create_controls_tab();
    auto& accessories = recompui::config::create_config_tab("Accessories", "accessories", true);
    using bar::input_config::PakType;
    const auto input_settings = bar::input_config::get();
    for (int port = 0; port < kMaxPlayers; ++port) {
        const std::string key = "player_" + std::to_string(port + 1) + "_pak";
        accessories.add_enum_option(key, "Player " + std::to_string(port + 1) + " Pak",
            "Controller Pak stores race records and ghosts. Rumble Pak enables force feedback. None removes the accessory.",
            {{PakType::None, "None"}, {PakType::ControllerPak, "Controller Pak"}, {PakType::RumblePak, "Rumble Pak"}},
            input_settings.ports[port].pak);
        accessories.add_option_change_callback(key,
            [port](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
                   recomp::config::OptionChangeContext context) {
                if (context == recomp::config::OptionChangeContext::Temporary) return;
                const auto* selected = std::get_if<uint32_t>(&value);
                if (selected == nullptr || *selected > uint32_t(PakType::RumblePak)) return;
                auto settings = bar::input_config::get();
                const auto pak = static_cast<PakType>(*selected);
                if (settings.ports[port].pak == pak) return;
                settings.ports[port].pak = pak;
                bar::input_config::set(settings);
                bar::input::set_config(settings);
            });
    }
    recompui::config::create_mods_tab();
    auto& cheats = recompui::config::create_config_tab("Cheats", "cheats", true);
    for (int i = 0; i < static_cast<int>(bar_cheats::Id::Count); ++i) {
        const auto id = static_cast<bar_cheats::Id>(i);
        const auto& info = bar_cheats::info(id);
        cheats.add_bool_option(info.id, info.label, info.hint, bar_cheats::get(id));
        cheats.add_option_change_callback(info.id,
            [id](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
                 recomp::config::OptionChangeContext context) {
                if (context == recomp::config::OptionChangeContext::Temporary) return;
                if (const auto* enabled = std::get_if<bool>(&value)) {
                    bar_cheats::set(id, *enabled);
                    bar_cheats::save();
                }
            });
    }
    recompui::config::finalize();

    // finalize() is what reads the JSON, so the saved values only exist after it. Draw Distance is
    // pushed every frame from pump_events (see push_draw_distance); the audio settings have real
    // change callbacks, which finalize() fires with OptionChangeContext::Load, so they need no
    // per-frame poll.
    push_draw_distance();

    // Input bindings. This is what makes the menus DRIVEABLE, and nothing else calls it:
    // load_controls_config() is the only public entry point that reaches
    // profiles::initialize_input_bindings(), which builds the key/button -> menu-action mapping
    // recompui navigates with. Without it the UI still renders and RmlUi still highlights on hover
    // (its own hit-testing), but no key or button maps to Accept/Back/navigate, so nothing can be
    // activated — the menu looks alive and is completely inert.
    //
    // The call also seeds sensible keyboard and controller defaults when the file does not exist
    // yet, and writes controls.json alongside the other config files.
    const std::filesystem::path controls_path = recomp::get_config_path() / "controls.json";
    const bool import_legacy = !std::filesystem::exists(controls_path) && g_bar_had_legacy_input_config;
    const bool loaded = recompinput::profiles::load_controls_config(controls_path);
    if (import_legacy) import_legacy_bindings();
    trace(loaded ? "controls config loaded" : "controls config created from defaults");

    trace("install: done");
}


namespace {

// Who is player one, and who is player two, three and four.
//
// The frontend's own answer is a modal in the Controls tab: it opens, each player presses a button
// on the pad they want, and the assignment is committed. That suits a game where which pad is which
// matters and cannot be guessed. Until someone has been through it, though, NOTHING is assigned --
// so a pad that is plugged in and working drives nothing, with no hint anywhere that a modal in a
// settings tab is what stands between it and the game.
//
// So the pads are assigned here instead, in the order SDL reports them, whenever that set changes:
// plug one in and it is player one, plug a second in and it is player two, up to the game's four.
// With no pad at all the keyboard becomes player one, so the game is still playable -- and player one
// keeps the keyboard whatever else it has, because get_n64_input merges a player's controller and
// keyboard profiles.
// scripts/patch-recompinput.py adds auto_assign_controllers; the modal still wins while it is open,
// for anyone who wants to choose.
void refresh_players() {
    // Every device SDL enumerates, logged whenever the count changes, so a pad that never reaches
    // `connected` below shows up with the reason -- no game-controller mapping, or an open that
    // failed. One line per change; it is what tells "the game ignores player two" apart from
    // "Windows is not connected to that pad".
    {
        static int last_count = -1;
        const int count = SDL_NumJoysticks();
        if (count != last_count) {
            last_count = count;
            std::fprintf(stderr, "[BeetleRecomp] SDL sees %d joystick%s\n", count, count == 1 ? "" : "s");
            for (int i = 0; i < count; ++i) {
                std::fprintf(stderr, "[BeetleRecomp]   #%d \"%s\" game_controller=%d\n",
                             i, SDL_JoystickNameForIndex(i), (int)SDL_IsGameController(i));
            }
            std::fflush(stderr);
        }
    }
    std::vector<SDL_GameController*> connected;
    static std::unordered_map<SDL_JoystickID, SDL_GameController*> owned_controllers;
    std::vector<SDL_JoystickID> connected_ids;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        auto it = owned_controllers.find(id);
        SDL_GameController* pad = it != owned_controllers.end() ? it->second : SDL_GameControllerOpen(i);
        if (pad != nullptr) {
            owned_controllers.emplace(id, pad);
            connected.push_back(pad);
            connected_ids.push_back(id);
        }
    }
    // SDL Open increments a reference count even when returning an existing handle.
    // Hold one reference per connected device, and release it on unplug.
    for (auto it = owned_controllers.begin(); it != owned_controllers.end();) {
        if (std::find(connected_ids.begin(), connected_ids.end(), it->first) == connected_ids.end()) {
            SDL_GameControllerClose(it->second);
            it = owned_controllers.erase(it);
        } else ++it;
    }

    // The first call always assigns, even with nothing connected. Comparing against the last set
    // alone is what breaks input entirely on a machine with no pad attached: an empty set matches an
    // empty set, so nothing is ever assigned, player one does not exist, and the game is told it has
    // no controller at all -- keyboard included.
    //
    // Each pad's controller profile is part of the comparison too. auto_assign_controllers gives a
    // player the profile recompinput created for its pad when SDL reported it; a pad assigned before
    // that profile existed would drive nothing -- player two present, its buttons mapped to nothing --
    // and, with the pad set unchanged, would never be looked at again. Re-assigning once the profile
    // appears closes that gap.
    std::vector<int> pad_profiles;
    for (SDL_GameController* pad : connected) {
        pad_profiles.push_back(recompinput::profiles::get_controller_profile_index_from_sdl_controller(pad));
    }
    static bool assigned_once = false;
    static std::vector<SDL_JoystickID> assigned;
    static std::vector<int> assigned_profiles;
    if (assigned_once && connected_ids == assigned && pad_profiles == assigned_profiles) {
        return;
    }
    assigned_once = true;
    assigned = connected_ids;
    assigned_profiles = pad_profiles;

    bool has_imported_slots = false;
    for (int port = 0; port < kMaxPlayers; ++port) {
        has_imported_slots |= recompinput::profiles::get_input_profile_by_key(
            "legacy_port_" + std::to_string(port) + "_keyboard") >= 0;
        has_imported_slots |= recompinput::profiles::get_input_profile_by_key(
            "legacy_port_" + std::to_string(port) + "_controller") >= 0;
    }
    if (has_imported_slots) {
        const auto legacy = bar::input_config::get();
        std::array<recompinput::Player, kMaxPlayers> slots{};
        std::array<bool, kMaxPlayers> reserved{};
        std::vector<bool> used(connected.size());
        for (int port = 0; port < kMaxPlayers; ++port) {
            const auto& old = legacy.ports[port];
            if (!old.connected || old.device.type == bar::input_config::DeviceType::None) continue;
            reserved[port] = true;
            if (old.device.type == bar::input_config::DeviceType::Keyboard) {
                slots[port].keyboard_enabled = true;
                continue;
            }
            for (size_t i = 0; i < connected.size(); ++i) {
                if (used[i]) continue;
                char guid[33]{};
                SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(connected[i])), guid, sizeof(guid));
                if (old.device.guid != guid) continue;
                const auto identity = recompinput::profiles::get_guid_from_sdl_controller(connected[i]);
                const std::string uid = std::string(guid) + "!" + identity.serial;
                if (!old.device.uid.empty() && old.device.uid != uid) continue;
                slots[port].controller = connected[i];
                used[i] = true;
                break;
            }
        }
        // New pads may use free slots, but cannot steal an imported device's slot
        // while that device is temporarily unplugged.
        for (size_t i = 0; i < connected.size(); ++i) {
            if (used[i]) continue;
            for (int port = 0; port < kMaxPlayers; ++port) {
                if (reserved[port] || slots[port].controller != nullptr || slots[port].keyboard_enabled) continue;
                slots[port].controller = connected[i];
                break;
            }
        }
        // Retain a keyboard fallback when player one has no imported keyboard map.
        if (!reserved[0]) slots[0].keyboard_enabled = true;
        recompinput::players::auto_assign_players(slots.data(), slots.size());
    } else {
        recompinput::players::auto_assign_controllers(connected.data(), connected.size());
    }

    std::fprintf(stderr, "[BeetleRecomp] %zu controller%s connected; assigned to "
                         "%zu player%s\n",
                 connected.size(), connected.size() == 1 ? "" : "s",
                 recompinput::players::get_number_of_assigned_players(),
                 recompinput::players::get_number_of_assigned_players() == 1 ? "" : "s");
    for (size_t i = 0; i < connected.size(); i++) {
        std::fprintf(stderr, "[BeetleRecomp]   player %zu: %s (controller profile %d)\n",
                     i + 1, SDL_GameControllerName(connected[i]), pad_profiles[i]);
    }
    std::fflush(stderr);
}

} // namespace

void bar::frontend::pump_events() {
    // recompinput owns the SDL pump: it polls, filters, forwards to recompui, and applies cursor
    // visibility / relative-mouse mode from recompui's flag (recompui itself never calls SDL).
    recompinput::handle_events();

    // poll_inputs() is what fills recompinput's keyboard snapshot from SDL, and like update_rumble
    // it is exposed for the port to call rather than called by the library itself. Without it,
    // InputState.keys stays null and every keyboard binding reads as unpressed -- while a pad keeps
    // working, because the controller path asks the player's own SDL handle directly. The result is
    // a dead keyboard beside a perfect gamepad, with nothing in the settings to suggest why.
    recompinput::poll_inputs();

    refresh_players();

    // Profiles and SDL handles belong to the event/UI thread. Publish coherent samples
    // for the game's controller thread instead of reading mutable profiles there.
    for (int port = 0; port < kMaxPlayers; ++port) {
        uint16_t buttons = 0;
        float x = 0.0f, y = 0.0f;
        const bool assigned = recompinput::players::get_player_is_assigned(port);
        if (assigned) recompinput::profiles::get_n64_input(port, &buttons, &x, &y);
        if (!std::isfinite(x)) x = 0.0f;
        if (!std::isfinite(y)) y = 0.0f;
        const auto sx = static_cast<uint8_t>(static_cast<int8_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 80.0f)));
        const auto sy = static_cast<uint8_t>(static_cast<int8_t>(std::lround(std::clamp(y, -1.0f, 1.0f) * 80.0f)));
        const uint64_t sample = buttons | (uint64_t(sx) << 16) | (uint64_t(sy) << 24) | (uint64_t(assigned) << 32);
        g_port_samples[port].store(sample, std::memory_order_relaxed);
    }
    g_menu_capturing.store(recompui::is_context_capturing_input(), std::memory_order_relaxed);

    // Rumble. BAR pulses its motor to set the strength, so the port models the motor
    // (src/main/bar_rumble.cpp) and sends each player's pad the resulting level, scaled by the General
    // tab's Rumble Strength, on both of its motors. recompinput::update_rumble is not called on that
    // path: it samples an on/off flag once per frame, which missed the pulses, and it would write its
    // own strength over this one. BAR_RUMBLE_RAW=1 goes back to it (A/B).
    if (bar::rumble::raw_mode()) {
        for (int port = 0; port < kMaxPlayers; ++port) {
            recompinput::set_rumble(port, g_raw_rumble[port].load(std::memory_order_relaxed));
        }
        recompinput::update_rumble();
    } else {
        uint16_t strength[bar::rumble::kPorts];
        bool send[bar::rumble::kPorts];
        bar::rumble::step(recompui::config::general::get_rumble_strength(), bar_output_silenced(), strength, send);
        for (int port = 0; port < bar::rumble::kPorts && port < kMaxPlayers; port++) {
            if (!send[port] || !recompinput::players::get_player_is_assigned(port)) continue;
            SDL_GameController* pad = recompinput::players::get_player(port).controller;
            if (pad != nullptr) {
                SDL_GameControllerRumble(pad, strength[port], strength[port],
                                         strength[port] != 0 ? bar::rumble::kSendDurationMs : 0);
            }
        }
    }

    // Pick up Draw Distance as soon as the player applies it. See push_draw_distance().
    push_draw_distance();
}

bool bar::frontend::port_assigned(int port) {
    if (port < 0 || port >= kMaxPlayers) {
        return false;
    }
    // Fresh configurations receive a keyboard on player one in refresh_players.
    // Imported configurations can reserve disconnected ports or use another keyboard
    // slot; report their actual assignments instead of inventing a player-one device.
    return (g_port_samples[port].load(std::memory_order_relaxed) >> 32) != 0;
}

void bar::frontend::set_port_rumble(int port, bool on) {
    if (port < 0 || port >= kMaxPlayers) {
        return;
    }
    g_raw_rumble[port].store(on, std::memory_order_relaxed);
}

uint16_t bar::frontend::poll_port(int port, int8_t* stick_x, int8_t* stick_y) {
    if (stick_x != nullptr) *stick_x = 0;
    if (stick_y != nullptr) *stick_y = 0;
    if (!port_assigned(port)) {
        return 0;
    }

    const uint64_t sample = g_port_samples[port].load(std::memory_order_relaxed);
    if (stick_x != nullptr) *stick_x = static_cast<int8_t>((sample >> 16) & 0xFF);
    if (stick_y != nullptr) *stick_y = static_cast<int8_t>((sample >> 24) & 0xFF);
    return static_cast<uint16_t>(sample);
}

bool bar::frontend::menu_capturing_input() {
    return g_menu_capturing.load(std::memory_order_relaxed);
}

std::unique_ptr<ultramodern::renderer::RendererContext>
bar::frontend::create_render_context(uint8_t* rdram,
                                     ultramodern::renderer::WindowHandle window_handle,
                                     bool developer_mode) {
    // Console presentation: present strictly from the VI origin, at VI time, exactly as hardware
    // does. BAR's main-menu film-roll transition steps the VI origin across a framebuffer without
    // ever redrawing, so any mode that presents only freshly-rendered content collapses the pan to
    // an instant swap. Do not "optimise" this to PresentEarly/SkipBuffering.
    trace("create_render_context: begin");

    // RT64's developer mode, on unconditionally.
    //
    // Every path to RT64's debug UI is gated on it: the F1 key handler, the event filter RT64
    // installs for itself, and State::inspect() at the other end -- which is also where the port's
    // HUD inspector is drawn. A debug menu that only exists in a build made for it is a debug menu
    // nobody has when they need it; the person looking at a misplaced HUD element is running the
    // game they downloaded.
    //
    // Nothing is drawn until F1 is pressed: RT64 creates its inspector on the keystroke and
    // State::inspect() returns immediately while there is none, so the cost of leaving this on is a
    // null check per frame. F2 is unbound in the fork for the same reason -- see
    // lib/rt64/src/hle/rt64_application.cpp.
    (void)developer_mode;
    auto ctx = recompui::renderer::create_render_context(
        rdram, window_handle, ultramodern::renderer::PresentationMode::Console, true);

    trace(ctx ? "create_render_context: ok" : "create_render_context: returned null");
    return ctx;
}
