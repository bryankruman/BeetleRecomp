#define SDL_MAIN_HANDLED
#include "main/bar_input.hpp"
#include "game/config.hpp"
#include "recompinput/profiles.h"
#include "../lib/RecompFrontend/recompui/src/composites/ui_assign_players_modal.h"
#include "../lib/RecompFrontend/recompui/src/config/ui_config_page_controls.h"

#include <chrono>
#include <climits>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static fs::path test_directory;
namespace bar::config {
const fs::path& get_app_config_directory() { return test_directory; }
}
namespace bar::input_config {
InputConfig default_input_config() { return {}; }
const InputInfo& input_info(N64Input) { throw std::logic_error("unexpected live input call"); }
const BindingProfile* find_profile(const InputConfig&, const std::string&) { throw std::logic_error("unexpected live input call"); }
}

// The harness exercises save storage, templates, and profile identity without a running UI.
// JSON I/O and default mappings below are local fixtures; live input/UI paths fail immediately.
namespace recompinput {
const std::vector<InputField>& get_default_mapping_for_input(InputDevice device, GameInput input) {
    static const auto defaults = [] {
        std::array<std::array<std::vector<InputField>, num_game_inputs>, 2> values;
        for (size_t d = 0; d < values.size(); ++d) {
            for (size_t i = 0; i < num_game_inputs; ++i) {
                values[d][i] = { InputField{d == 0 ? InputType::ControllerDigital : InputType::Keyboard, static_cast<int32_t>(100 + i)} };
            }
        }
        return values;
    }();
    return defaults.at(static_cast<size_t>(device)).at(static_cast<size_t>(input));
}
bool game_input_disabled() { throw std::logic_error("unexpected UI call"); }
float get_input_analog(int, std::span<const InputField>) { throw std::logic_error("unexpected analog call"); }
bool get_input_digital(int, std::span<const InputField>) { throw std::logic_error("unexpected digital call"); }
float get_input_analog(int, const InputField&) { throw std::logic_error("unexpected analog field call"); }
bool get_input_digital(int, const InputField&) { throw std::logic_error("unexpected digital field call"); }
void apply_joystick_deadzone(float, float, float*, float*) { throw std::logic_error("unexpected deadzone call"); }
const std::string& get_game_input_enum_name(GameInput input) {
    static const auto names = [] {
        std::array<std::string, num_game_inputs> values;
        for (size_t i = 0; i < values.size(); ++i) values[i] = "input_" + std::to_string(i);
        return values;
    }();
    return names.at(static_cast<size_t>(input));
}
bool save_json_with_backups(const fs::path& path, const nlohmann::json& value) {
    std::ofstream stream(path);
    stream << value.dump(2);
    return stream.good();
}
bool read_json_with_backups(const fs::path& path, nlohmann::json& value) {
    std::ifstream stream(path);
    if (!stream) return false;
    stream >> value;
    return stream.good() || stream.eof();
}
void purge_deferred_controller_profiles() { throw std::logic_error("unexpected deferred profile call"); }
SDL_GameController* get_controller_from_joystick_id(SDL_JoystickID) { throw std::logic_error("unexpected UI device call"); }
}
namespace ultramodern {
std::chrono::high_resolution_clock::duration time_since_start() { throw std::logic_error("unexpected live clock call"); }
}
namespace recompui {
ConfigPageControls* controls_page = nullptr;
void AssignPlayersModal::close() { throw std::logic_error("unexpected modal call"); }
void ConfigPageControls::force_update() { throw std::logic_error("unexpected controls UI call"); }
}

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
static void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(stream.good(), "fixture write failed");
}

int main() {
    try {
        // Keep fixtures under the repository's ignored review directory; never load real saves.
        test_directory = fs::current_path() / ".review" /
            ("runtime-input-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(test_directory), "fixture directory must be new");
        const auto pak = [](int port) { return test_directory / ("mempak_p" + std::to_string(port) + ".pak"); };
        const std::vector<uint8_t> short_save(17, 0xA5), long_save(32769, 0x5A), existing_save(32768, 0x6B);
        write_file(pak(1), short_save);
        write_file(pak(2), long_save);
        write_file(pak(3), existing_save);
        uint8_t data[32]{};
        require(bar::input::mempak_read(0, 1023, data), "fresh pak last block must exist");
        require(fs::file_size(pak(0)) == 32768, "fresh pak size incorrect");
        require(!bar::input::mempak_read(0, INT_MAX, data), "large block must be rejected");
        require(!bar::input::mempak_read(-1, 0, data), "negative port must be rejected");
        require(!bar::input::mempak_read(4, 0, data), "fifth port must be rejected");
        require(!bar::input::mempak_write(0, 1024, data), "out-of-range write must be rejected");
        require(!bar::input::mempak_read(0, 0, nullptr), "null buffer must be rejected");
        require(!bar::input::mempak_read(1, 0, data), "truncated save must be rejected");
        require(!bar::input::mempak_write(1, 0, data), "truncated save must not be overwritten");
        require(!bar::input::mempak_read(2, 0, data), "oversized save must be rejected");
        require(bar::input::mempak_read(3, 0, data) && data[0] == 0x6B, "existing save must load");
        std::fill(std::begin(data), std::end(data), 0x37);
        require(bar::input::mempak_write(0, 9, data), "valid write failed");
        const fs::path temp = pak(0).string() + ".tmp";
        require(fs::create_directory(temp), "failure fixture failed");
        const auto before_failure = read_file(pak(0));
        bar::input::mempak_flush_all();
        require(read_file(pak(0)) == before_failure, "failed save changed existing file");
        fs::remove(temp); // Empty fixture directory only.
        bar::input::mempak_flush_all();
        const auto saved = read_file(pak(0));
        require(saved.size() == 32768 && saved[9 * 32] == 0x37, "failed save was not retried");
        require(read_file(pak(1)) == short_save && read_file(pak(2)) == long_save, "invalid saves changed");

        using namespace recompinput;
        ControllerGUID first{}, second{};
        first.vendor = second.vendor = 123;
        first.product = second.product = 456;
        first.serial = "#1";
        second.serial = "#2";
        const int a = profiles::add_input_profile("test_a", "A", InputDevice::Controller, true);
        const int b = profiles::add_input_profile("test_b", "B", InputDevice::Controller, true);
        const int first_id = profiles::add_controller(first, a);
        const int second_id = profiles::add_controller(second, b);
        require(first_id != second_id, "identical pads collapsed into one identity");
        const nlohmann::json serialized = first;
        auto restored = serialized.get<ControllerGUID>();
        restored.hash = UINT64_MAX; // Runtime hash is deliberately not serialized.
        require(profiles::get_controller_by_guid(restored) == first_id, "restored identity depended on hash");
        require(profiles::get_controller_profile_index(second_id) == b, "second pad lost its custom profile");
        ControllerGUID legacy = first;
        legacy.product = 789;
        legacy.serial.clear();
        const int legacy_id = profiles::add_controller(legacy, b);
        legacy.serial = "#1";
        require(profiles::get_controller_by_guid(legacy) == legacy_id, "legacy no-serial profile was lost");
        require(profiles::get_controller_profile_index(legacy_id) == b, "legacy profile assignment changed");
        legacy.serial = "#2";
        require(profiles::get_controller_by_guid(legacy) == -1, "legacy profile leaked to a second pad");
        profiles::initialize_input_bindings();
        const int working = profiles::add_input_profile("test_working", "Built-in", InputDevice::Controller, false);
        const int keys = profiles::add_input_profile("test_keys", "Test keyboard", InputDevice::Keyboard, false);
        const InputField source_a{InputType::ControllerDigital, 7};
        const InputField source_menu{InputType::ControllerDigital, 9};
        const InputField live_menu{InputType::ControllerDigital, 11};
        profiles::set_input_binding(working, GameInput::A, 0, source_a);
        profiles::set_input_binding(working, GameInput::ACCEPT_MENU, 0, source_menu);
        int saved_profile = -1;
        require(!profiles::save_input_profile_as(-1, "Invalid", saved_profile), "invalid source accepted");
        require(!profiles::save_input_profile_as(working, "  ", saved_profile), "blank name accepted");
        require(!profiles::save_input_profile_as(working, std::string(65, 'x'), saved_profile), "long name accepted");
        require(!profiles::save_input_profile_as(working, "Bad\nName", saved_profile), "control character accepted");
        require(profiles::save_input_profile_as(working, "  Race template  ", saved_profile), "save-as failed");
        const int race = saved_profile;
        const std::string race_key = profiles::get_input_profile_key(race);
        require(profiles::get_input_profile_name(race) == "Race template" && race_key != "Race template", "name/key separation or trim failed");
        require(profiles::get_input_binding(race, GameInput::ACCEPT_MENU, 0) == source_menu, "save-as lost full snapshot");
        require(!profiles::save_input_profile_as(working, "RACE TEMPLATE", saved_profile), "duplicate name accepted without overwrite");
        require(!profiles::save_input_profile_as(working, "Built-in", saved_profile, true), "built-in profile overwritten");
        int keyboard_template = -1;
        require(profiles::save_input_profile_as(keys, "Race template", keyboard_template), "same name on another device rejected");
        require(!profiles::apply_input_profile(race, keys), "cross-device copy accepted");
        require(!profiles::rename_input_profile(working, "New built-in name") && !profiles::delete_input_profile(working), "built-in mutable as template");
        require(profiles::rename_input_profile(race, "  Race renamed ") && profiles::get_input_profile_key(race) == race_key, "rename changed key");
        require(!profiles::rename_input_profile(race, "Built-in"), "rename collided with built-in");
        profiles::set_input_binding(b, GameInput::ACCEPT_MENU, 0, live_menu);
        require(profiles::apply_input_profile(race, b), "gameplay copy failed");
        require(profiles::get_input_binding(b, GameInput::A, 0) == source_a &&
                profiles::get_input_binding(b, GameInput::ACCEPT_MENU, 0) == live_menu, "apply changed menu or missed gameplay");
        require(profiles::apply_input_profile(race, b, true) &&
                profiles::get_input_binding(b, GameInput::ACCEPT_MENU, 0) == source_menu, "explicit full copy failed");
        profiles::set_input_binding(b, GameInput::ACCEPT_MENU, 0, live_menu);
        int disposable = -1;
        require(profiles::save_input_profile_as(working, "Disposable", disposable), "delete fixture save failed");
        const auto deleted_key = profiles::get_input_profile_key(disposable);
        ControllerGUID borrowed{};
        borrowed.serial = "template-user";
        const int borrowed_id = profiles::add_controller(borrowed, disposable);
        profiles::set_input_profile_for_player(3, disposable, InputDevice::Controller);
        const int stable_b_index = b;
        require(profiles::delete_input_profile(disposable), "custom delete failed");
        const int detached_controller = profiles::get_controller_profile_index(borrowed_id);
        const int detached_player = profiles::get_input_profile_for_player(3, InputDevice::Controller);
        require(detached_controller != disposable && detached_player != disposable && detached_controller != detached_player,
                "delete retained template/shared references");
        require(profiles::get_input_binding(detached_controller, GameInput::A, 0) == source_a &&
                profiles::get_input_binding(detached_player, GameInput::ACCEPT_MENU, 0) == source_menu, "delete lost live mappings");
        require(profiles::get_input_profile_by_key(deleted_key) == -1 && !profiles::is_input_profile_custom(disposable), "deleted template remains addressable");
        require(!profiles::apply_input_profile(disposable, b) && !profiles::delete_input_profile(disposable), "tombstone remained active");
        const auto custom = profiles::get_indices_for_custom_profiles(InputDevice::Controller);
        require(std::find(custom.begin(), custom.end(), disposable) == custom.end() && b == stable_b_index,
                "delete changed selectors or indices");
        profiles::set_input_profile_for_player(4, race, InputDevice::Controller);
        profiles::set_input_binding(working, GameInput::A, 0, InputField{InputType::ControllerDigital, 13});
        require(profiles::save_input_profile_as(working, "Race renamed", saved_profile, true) && saved_profile == race &&
                profiles::get_input_profile_key(race) == race_key, "explicit overwrite changed template identity");
        require(profiles::get_input_binding(profiles::get_input_profile_for_player(4, InputDevice::Controller), GameInput::A, 0) == source_a,
                "overwrite changed a live template user");
        const auto controls_path = test_directory / "controls-fixture.json";
        require(profiles::save_controls_config(controls_path), "profile serialization failed");
        nlohmann::json controls;
        { std::ifstream stream(controls_path); stream >> controls; }
        for (const auto& profile : controls["profiles"]) require(profile["key"] != deleted_key, "tombstone serialized");
        profiles::clear_input_binding(race, GameInput::A);
        require(profiles::load_controls_config(controls_path) && profiles::get_input_profile_by_key(race_key) == race &&
                profiles::get_input_binding(race, GameInput::A, 0).input_id == 13, "template serialization roundtrip failed");
        for (auto& profile : controls["profiles"]) {
            if (profile["key"] == race_key) {
                profile["mappings"][get_game_input_enum_name(GameInput::ACCEPT_MENU)] = nlohmann::json::array();
                profile["mappings"].erase(get_game_input_enum_name(GameInput::BACK_MENU));
            }
        }
        { std::ofstream stream(controls_path); stream << controls; }
        profiles::set_input_binding(race, GameInput::BACK_MENU, 1, source_menu);
        require(profiles::load_controls_config(controls_path), "partial config reload failed");
        require(profiles::get_input_binding(race, GameInput::ACCEPT_MENU, 0).is_empty() &&
                profiles::get_input_binding(race, GameInput::ACCEPT_MENU, 1).is_empty(), "explicit cleared menu was default-filled");
        require(profiles::get_input_binding(race, GameInput::BACK_MENU, 0) == get_default_mapping_for_input(InputDevice::Controller, GameInput::BACK_MENU)[0] &&
                profiles::get_input_binding(race, GameInput::BACK_MENU, 1).is_empty(), "missing menu did not get complete defaults");
        auto custom_only = controls;
        auto& rows = custom_only["profiles"];
        rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto& row) {
            return row["key"] == "keyboard_sp" || row["key"] == "controller_sp";
        }), rows.end());
        { std::ofstream stream(controls_path); stream << custom_only; }
        profiles::clear_input_binding(profiles::get_sp_controller_profile_index(), GameInput::A);
        require(profiles::load_controls_config(controls_path) &&
                profiles::get_input_binding(profiles::get_sp_controller_profile_index(), GameInput::A, 0) ==
                    get_default_mapping_for_input(InputDevice::Controller, GameInput::A)[0], "omitted SP profile stayed blank");

        require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "SDL controller initialization failed");
        const int virtual_first = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        const int virtual_second = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        require(virtual_first >= 0 && virtual_second >= 0, "virtual controller fixtures failed");
        auto* pad_first = SDL_GameControllerOpen(virtual_first);
        auto* pad_second = SDL_GameControllerOpen(virtual_second);
        require(pad_first && pad_second, "virtual controllers could not open");
        const auto guid_first = profiles::get_guid_from_sdl_controller(pad_first);
        const auto guid_second = profiles::get_guid_from_sdl_controller(pad_second);
        require(guid_first.serial == "#1" && guid_second.serial == "#2", "no-serial controller ordinals collided");
        const int keyboard_p1 = profiles::add_input_profile("legacy_port_0_keyboard", "Player 1 keys", InputDevice::Keyboard, true);
        const int keyboard_p3 = profiles::add_input_profile("legacy_port_2_keyboard", "Player 3 keys", InputDevice::Keyboard, true);
        std::array<Player, 4> slots{};
        slots[0].keyboard_enabled = true;
        slots[2].keyboard_enabled = true;
        players::auto_assign_players(slots.data(), slots.size());
        require(players::get_player_is_assigned(0) && players::get_player_is_assigned(2), "imported keyboard slots were lost");
        require(!players::get_player_is_assigned(1) && !players::get_player_is_assigned(3), "empty imported slot became assigned");
        require(profiles::get_input_profile_for_player(0, InputDevice::Keyboard) == keyboard_p1 &&
                profiles::get_input_profile_for_player(2, InputDevice::Keyboard) == keyboard_p3, "per-player keyboard maps were mixed");
        slots[0].keyboard_enabled = false;
        players::auto_assign_players(slots.data(), slots.size());
        require(!players::get_player_is_assigned(0) && players::get_player_is_assigned(2), "empty first port became assigned");
        require(profiles::get_input_profile_for_player(0, InputDevice::Keyboard) == -1, "empty first port retained its keyboard profile");
        slots[0].keyboard_enabled = true;
        slots[2].keyboard_enabled = false;
        slots[2].controller = pad_second;
        profiles::add_controller(guid_second, b);
        players::auto_assign_players(slots.data(), slots.size());
        require(players::get_player(2).controller == pad_second && !players::get_player_is_assigned(1), "controller slot was compacted");
        require(profiles::get_input_profile_for_player(2, InputDevice::Controller) == b &&
                profiles::get_input_profile_for_player(2, InputDevice::Keyboard) == -1, "old keyboard mapping survived controller assignment");
        require(profiles::apply_input_profile_for_player(2, race), "per-pad template apply failed");
        const int pad_working = profiles::get_input_profile_for_player(2, InputDevice::Controller);
        require(pad_working != race && pad_working != b && !profiles::is_input_profile_custom(pad_working), "pad retained shared template");
        require(profiles::get_controller_profile_index_from_sdl_controller(pad_second) == pad_working &&
                profiles::get_input_binding(pad_working, GameInput::A, 0).input_id == 13 &&
                profiles::get_input_binding(pad_working, GameInput::ACCEPT_MENU, 0) == live_menu, "per-pad apply lost persistence or menu controls");
        require(profiles::save_controls_config(controls_path), "pad association save failed");
        profiles::add_controller(guid_second, b);
        require(profiles::load_controls_config(controls_path) &&
                profiles::get_controller_profile_index_from_sdl_controller(pad_second) == pad_working, "pad working association did not roundtrip");
        require(profiles::delete_input_profile(race) &&
                profiles::get_input_binding(pad_working, GameInput::A, 0).input_id == 13, "template deletion changed applied pad map");
        require(!profiles::apply_input_profile_for_player(-1, b) && !profiles::apply_input_profile_for_player(2, keyboard_template),
                "invalid player/device template accepted");
        int attached_template = -1;
        require(profiles::save_input_profile_as(b, "Attached template", attached_template), "attached delete fixture failed");
        profiles::add_controller(guid_second, attached_template);
        profiles::set_input_profile_for_player(2, attached_template, InputDevice::Controller);
        require(profiles::delete_input_profile(attached_template), "attached template delete failed");
        require(profiles::get_input_profile_for_player(2, InputDevice::Controller) ==
                profiles::get_controller_profile_index_from_sdl_controller(pad_second), "delete split live and persisted controller maps");
        const int attached_live = profiles::get_input_profile_for_player(2, InputDevice::Controller);
        profiles::set_input_binding(attached_live, GameInput::A, 0, source_a);
        require(profiles::get_input_binding(profiles::get_controller_profile_index_from_sdl_controller(pad_second), GameInput::A, 0) == source_a,
                "editing detached live profile missed device map");
        // Device-specific defaults apply only when explicitly reset, never during
        // discovery or JSON load. An ordinary pad/profile must keep generic defaults.
        profiles::set_controller_default_mapping_callback([pad_second](SDL_GameController* pad, GameInput input, std::vector<InputField>& fields) {
            if (pad != pad_second || input != GameInput::C_UP) return false;
            fields = {InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y)};
            return true;
        });
        const auto custom_c = InputField::controller_digital(SDL_CONTROLLER_BUTTON_B);
        profiles::set_input_binding(attached_live, GameInput::C_UP, 0, custom_c);
        require(profiles::save_controls_config(controls_path) && profiles::load_controls_config(controls_path), "device default fixture roundtrip failed");
        require(profiles::get_input_binding(attached_live, GameInput::C_UP, 0) == custom_c, "device defaults replaced saved customization");
        profiles::reset_input_binding(attached_live, InputDevice::Controller, GameInput::C_UP);
        require(profiles::get_input_binding(attached_live, GameInput::C_UP, 0) == InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y), "single reset lost device default");
        profiles::set_input_binding(attached_live, GameInput::C_UP, 0, custom_c);
        profiles::reset_profile_bindings(attached_live, InputDevice::Controller);
        require(profiles::get_input_binding(attached_live, GameInput::C_UP, 0) == InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y), "full reset lost device default");
        profiles::reset_input_binding(a, InputDevice::Controller, GameInput::C_UP);
        require(profiles::get_input_binding(a, GameInput::C_UP, 0) == get_default_mapping_for_input(InputDevice::Controller, GameInput::C_UP).front(), "device defaults leaked to unrelated profile");
        profiles::set_controller_default_mapping_callback({});
        require(SDL_JoystickDetachVirtual(virtual_first) == 0, "virtual unplug failed");
        require(profiles::get_guid_from_sdl_controller(pad_second).serial == "#2", "remaining pad changed identity on unplug");
        SDL_GameControllerClose(pad_first);
        SDL_GameControllerClose(pad_second);
        for (int i = SDL_NumJoysticks() - 1; i >= 0; --i) {
            if (SDL_JoystickIsVirtual(i)) SDL_JoystickDetachVirtual(i);
        }
        SDL_Quit();
        std::cout << "PASS: save bounds, corrupt-save preservation, failed-write retry, controller identity, imported player slots, template lifecycle, and profile roundtrips\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
