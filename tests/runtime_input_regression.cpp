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

// The harness exercises save storage and profile identity without a running UI.
// Fail immediately if any excluded UI/device path is accidentally exercised.
namespace recompinput {
const std::vector<InputField>& get_default_mapping_for_input(InputDevice, GameInput) { throw std::logic_error("unexpected mapping call"); }
bool game_input_disabled() { throw std::logic_error("unexpected UI call"); }
float get_input_analog(int, std::span<const InputField>) { throw std::logic_error("unexpected analog call"); }
bool get_input_digital(int, std::span<const InputField>) { throw std::logic_error("unexpected digital call"); }
void apply_joystick_deadzone(float, float, float*, float*) { throw std::logic_error("unexpected deadzone call"); }
const std::string& get_game_input_enum_name(GameInput) { throw std::logic_error("unexpected menu call"); }
bool save_json_with_backups(const fs::path&, const nlohmann::json&) { throw std::logic_error("unexpected JSON file call"); }
bool read_json_with_backups(const fs::path&, nlohmann::json&) { throw std::logic_error("unexpected JSON file call"); }
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
        require(SDL_JoystickDetachVirtual(virtual_first) == 0, "virtual unplug failed");
        require(profiles::get_guid_from_sdl_controller(pad_second).serial == "#2", "remaining pad changed identity on unplug");
        SDL_GameControllerClose(pad_first);
        SDL_GameControllerClose(pad_second);
        for (int i = SDL_NumJoysticks() - 1; i >= 0; --i) {
            if (SDL_JoystickIsVirtual(i)) SDL_JoystickDetachVirtual(i);
        }
        SDL_Quit();
        std::cout << "PASS: save bounds, corrupt-save preservation, failed-write retry, controller identity, and imported player slots\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
