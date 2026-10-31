#include "recompinput/recompinput.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompinput/input_events.h"
#include "ultramodern/ultramodern.hpp"
#include "composites/ui_assign_players_modal.h"
#include "config/ui_config_page_controls.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace recompinput {

void Player::button_pressed() {
    last_button_press_timestamp = ultramodern::time_since_start();
}

using players_array = std::array<Player, max_num_players_supported>;
class PlayerArray : public players_array {
private:
    size_t count = 0;
public:
    PlayerArray() : players_array{}, count(0) {}
    PlayerArray(const PlayerArray& other) : players_array(other), count(other.count) {}

    size_t get_count() const {
        return count;
    }

    void add_keyboard_player() {
        if (count < max_num_players_supported) {
            Player &player = (*this)[count++];
            player = Player();
            player.keyboard_enabled = true;
            player.button_pressed();
        }
    }

    void add_controller_player(SDL_GameController* controller) {
        if (count < max_num_players_supported) {
            Player &player = (*this)[count++];
            player = Player();
            player.controller = controller;
            player.button_pressed();
        }
    }

    bool player_is_assigned(int player_index) const {
        return player_index >= 0 && size_t(player_index) < max_num_players_supported && size_t(player_index) < count &&
            ((*this)[player_index].controller != nullptr || (*this)[player_index].keyboard_enabled);
    }

    void assign_slot(size_t index, const Player& player) {
        if (index >= max_num_players_supported) return;
        (*this)[index] = player;
        if (player.controller != nullptr || player.keyboard_enabled) count = std::max(count, index + 1);
    }

    // Slots may have gaps (a deliberately empty port), so count is "highest slot + 1", not the
    // number of players. These helpers keep that invariant for the assignment editor.
    size_t assigned_count() const {
        size_t assigned = 0;
        for (size_t i = 0; i < count; i++) {
            assigned += player_is_assigned(int(i)) ? 1 : 0;
        }
        return assigned;
    }

    int first_free_slot(size_t limit) const {
        for (size_t i = 0; i < std::min(limit, max_num_players_supported); i++) {
            if (!player_is_assigned(int(i))) return int(i);
        }
        return -1;
    }

    void clear_slot(size_t index) {
        if (index >= max_num_players_supported) return;
        (*this)[index] = Player();
        recount();
    }

    void swap_slots(size_t a, size_t b) {
        if (a >= max_num_players_supported || b >= max_num_players_supported) return;
        std::swap((*this)[a], (*this)[b]);
        recount();
    }

private:
    void recount() {
        count = 0;
        for (size_t i = 0; i < max_num_players_supported; i++) {
            if (player_is_assigned_unbounded(i)) count = i + 1;
        }
    }

    bool player_is_assigned_unbounded(size_t index) const {
        return (*this)[index].controller != nullptr || (*this)[index].keyboard_enabled;
    }
};

static struct {
    bool single_player_mode = false;
    bool is_assigning = false;
    size_t min_players = 1;
    size_t max_players = 4;
    PlayerArray players;
    PlayerArray temp_players;
    // Slots filled during the current assignment session, oldest first, for "Undo".
    std::vector<int> join_order;

    PlayerArray& get_player_array(bool temp) {
        return temp ? temp_players : players;
    }

    bool queue_close_player_assignment_modal = false;
} PlayerState;

size_t players::get_number_of_assigned_players() {
    return PlayerState.players.get_count();
}

size_t players::get_max_number_of_players() {
    if (PlayerState.single_player_mode) {
        return 1;
    }
    return PlayerState.max_players;
}

bool players::is_single_player_mode() {
    return PlayerState.single_player_mode;
}

void players::set_single_player_mode(bool single_player) {
    if (!single_player && single_player != PlayerState.single_player_mode) {
        purge_deferred_controller_profiles();
    }

    PlayerState.single_player_mode = single_player;
}

void players::set_min_number_of_players(size_t min_players) {
    assert(min_players >= 1 && min_players <= PlayerState.max_players);
    PlayerState.min_players = min_players;
}

void players::set_max_number_of_players(size_t max_players) {
    assert(max_players >= PlayerState.min_players && max_players <= recompinput::max_num_players_supported);
    PlayerState.max_players = max_players;
}

void players::set_player_count_range(size_t min_players, size_t max_players) {
    assert(min_players >= 1 && max_players <= recompinput::max_num_players_supported && min_players <= max_players);
    PlayerState.min_players = min_players;
    PlayerState.max_players = max_players;
}

const recompinput::Player& players::get_player(int player_index, bool temp_player) {
    return PlayerState.get_player_array(temp_player)[player_index];
}

bool players::get_player_is_assigned(int player_index, bool temp_player) {
    return PlayerState.get_player_array(temp_player).player_is_assigned(player_index);
}

bool players::has_enough_players_assigned() {
    return PlayerState.players.assigned_count() >= PlayerState.min_players;
}

InputDevice players::get_player_input_device(int player_index, bool temp_player) {
    auto player_array = PlayerState.get_player_array(temp_player);
    if (!player_array.player_is_assigned(player_index)) {
        return InputDevice::COUNT;
    }

    const auto& assigned_player = player_array[player_index];
    if (assigned_player.controller != nullptr) {
        return InputDevice::Controller;
    } else if (assigned_player.keyboard_enabled) {
        return InputDevice::Keyboard;
    } else {
        return InputDevice::COUNT;
    }
}

// playerassignment start

bool playerassignment::is_active() {
    return PlayerState.is_assigning;
}

void playerassignment::start(void) {
    PlayerState.is_assigning = true;
    PlayerState.temp_players = PlayerArray{};
    PlayerState.join_order.clear();
}

void playerassignment::start_from_current(void) {
    PlayerState.is_assigning = true;
    PlayerState.temp_players = PlayerState.players;
    PlayerState.join_order.clear();
}

size_t playerassignment::get_assigned_count() {
    return PlayerState.temp_players.assigned_count();
}

int playerassignment::get_next_free_slot() {
    if (!playerassignment::is_active()) return -1;
    return PlayerState.temp_players.first_free_slot(players::get_max_number_of_players());
}

static void record_join(int slot) {
    if (slot >= 0) PlayerState.join_order.push_back(slot);
}

void playerassignment::remove_player(int player_index) {
    if (!playerassignment::is_active() || player_index < 0) return;
    PlayerState.temp_players.clear_slot(size_t(player_index));
    auto& order = PlayerState.join_order;
    order.erase(std::remove(order.begin(), order.end(), player_index), order.end());
}

bool playerassignment::can_undo() {
    return playerassignment::is_active() && !PlayerState.join_order.empty();
}

void playerassignment::undo_last_join() {
    if (!playerassignment::can_undo()) return;
    const int slot = PlayerState.join_order.back();
    PlayerState.join_order.pop_back();
    PlayerState.temp_players.clear_slot(size_t(slot));
}

void playerassignment::swap_players(int first, int second) {
    const int max_players = int(players::get_max_number_of_players());
    if (!playerassignment::is_active() || first == second || first < 0 || second < 0 ||
        first >= max_players || second >= max_players) return;
    PlayerState.temp_players.swap_slots(size_t(first), size_t(second));
    // Undo follows the player, not the slot number.
    for (int& slot : PlayerState.join_order) {
        if (slot == first) slot = second;
        else if (slot == second) slot = first;
    }
}

void playerassignment::stop(void) {
    PlayerState.is_assigning = false;
}

void players::auto_assign_controllers(SDL_GameController* const* controllers, size_t count) {
    if (PlayerState.is_assigning) {
        return;
    }

    PlayerArray assigned{};
    const size_t limit = players::get_max_number_of_players();
    for (size_t i = 0; i < count && assigned.get_count() < limit; i++) {
        if (controllers[i] != nullptr) {
            assigned.add_controller_player(controllers[i]);
        }
    }
    if (assigned.get_count() == 0) {
        assigned.add_keyboard_player();
    }

    PlayerState.players = assigned;

    // The same profile assignment commit_player_assignment performs, so that a
    // player's own remapping follows its pad into whichever slot it lands in.
    // Every slot's pair is reset first, so a keyboard that an earlier "Assign players" commit gave to
    // player two does not stay on player two once this hands the slots out again.
    for (int i = 0; i < (int)recompinput::max_num_players_supported; i++) {
        profiles::set_input_profile_for_player(i, -1, InputDevice::Controller);
        profiles::set_input_profile_for_player(i, -1, InputDevice::Keyboard);
    }
    for (int i = 0; i < (int)PlayerState.players.get_count(); i++) {
        Player &player = PlayerState.players[i];
        if (player.controller != nullptr) {
            int cont_profile_index = profiles::get_controller_profile_index_from_sdl_controller(player.controller);
            if (cont_profile_index >= 0) {
                profiles::set_input_profile_for_player(i, cont_profile_index, InputDevice::Controller);
            }
        }
    }

    // Player one keeps the keyboard as well, and it is the single-player
    // keyboard profile -- the one the controls tab edits and the one that has
    // the default bindings, where a freshly created multiplayer keyboard
    // profile has none. get_n64_input reads a player's controller and keyboard
    // profiles and merges them, so player one can use either at any moment.
    profiles::set_input_profile_for_player(0, profiles::get_sp_keyboard_profile_index(), InputDevice::Keyboard);
}

void players::auto_assign_players(const Player* slots, size_t count) {
    if (PlayerState.is_assigning || slots == nullptr) return;
    PlayerArray assigned{};
    const size_t limit = std::min(count, players::get_max_number_of_players());
    for (size_t i = 0; i < limit; ++i) assigned.assign_slot(i, slots[i]);
    PlayerState.players = assigned;
    for (size_t i = 0; i < recompinput::max_num_players_supported; ++i) {
        profiles::set_input_profile_for_player(int(i), -1, InputDevice::Controller);
        profiles::set_input_profile_for_player(int(i), -1, InputDevice::Keyboard);
        if (i >= limit || !assigned.player_is_assigned(int(i))) continue;
        if (slots[i].controller != nullptr) {
            profiles::set_input_profile_for_player(int(i),
                profiles::get_controller_profile_index_from_sdl_controller(slots[i].controller), InputDevice::Controller);
        }
        if (slots[i].keyboard_enabled) {
            int profile = profiles::get_input_profile_by_key("legacy_port_" + std::to_string(i) + "_keyboard");
            if (profile < 0) profile = profiles::get_sp_keyboard_profile_index();
            profiles::set_input_profile_for_player(int(i), profile, InputDevice::Keyboard);
        }
    }
}

void playerassignment::stop_and_close_modal() {
    playerassignment::stop();
    PlayerState.queue_close_player_assignment_modal = true;
}

void playerassignment::commit_player_assignment() {
    stop_and_close_modal();

    PlayerState.players = PlayerState.temp_players;

    // Local change (Beetle Adventure Racing port): each player's profile pair is rebuilt from
    // scratch, and the keyboard player uses the single-player keyboard profile.
    //
    // Upstream only ever SET the device a player was assigned, so whatever profile a player held
    // before survived the commit. auto_assign_controllers gives player one the keyboard, and the
    // keyboard is global state, so after assigning "pad = player 1, keyboard = player 2" the keys
    // still drove player one. The keyboard player, meanwhile, was handed a per-player multiplayer
    // keyboard profile, which is created with every binding cleared -- player two existed and
    // pressed nothing. Only one keyboard player can be assigned (see process_sdl_event), so the
    // overlap that clearing guards against cannot happen, and the single-player keyboard profile is
    // the one with the defaults and the one the controls tab edits. The keyboard drives only a
    // player added with "Add keyboard"; pad players do not share it. Slots can be empty (a
    // deliberate gap), and those get no profiles.
    for (int i = 0; i < (int)recompinput::max_num_players_supported; i++) {
        Player &player = PlayerState.players[i];
        int cont_profile_index = -1;
        int kb_profile_index = -1;
        if (!PlayerState.players.player_is_assigned(i)) {
            // Empty slot: no profiles.
        } else if (player.controller != nullptr) {
            // Pad players get no keyboard: it belongs to whoever was added with "Add keyboard".
            cont_profile_index = profiles::get_controller_profile_index_from_sdl_controller(player.controller);
        } else {
            kb_profile_index = profiles::get_sp_keyboard_profile_index();
        }
        profiles::set_input_profile_for_player(i, cont_profile_index, InputDevice::Controller);
        profiles::set_input_profile_for_player(i, kb_profile_index, InputDevice::Keyboard);
    }
}

void playerassignment::add_keyboard_player() {
    if (playerassignment::was_keyboard_assigned()) return;
    const int slot = playerassignment::get_next_free_slot();
    if (slot < 0) return;
    // The keyboard is one device: a pad player that was sharing it gives it up.
    for (size_t i = 0; i < PlayerState.temp_players.size(); i++) {
        if (PlayerState.temp_players[i].controller != nullptr) PlayerState.temp_players[i].keyboard_enabled = false;
    }
    Player player{};
    player.keyboard_enabled = true;
    player.button_pressed();
    PlayerState.temp_players.assign_slot(size_t(slot), player);
    record_join(slot);
}

bool playerassignment::met_assignment_requirements() {
    return PlayerState.temp_players.assigned_count() >= PlayerState.min_players;
}

bool playerassignment::is_blocking_input() {
    return playerassignment::is_active() && !playerassignment::met_assignment_requirements();
}

bool playerassignment::is_player_currently_assigning(int player_index) {
    return playerassignment::is_active() && player_index == playerassignment::get_next_free_slot();
}

bool playerassignment::was_keyboard_assigned() {
    size_t num_temp = PlayerState.temp_players.size();
    if (num_temp == 0) {
        return false;
    }

    // A keyboard-only player; a pad player sharing the keyboard does not count.
    for (size_t i = 0; i < num_temp; i++) {
        if (PlayerState.temp_players[i].keyboard_enabled && PlayerState.temp_players[i].controller == nullptr) {
            return true;
        }
    }

    return false;
}

static std::mutex controller_activity_mutex;
static std::unordered_set<SDL_JoystickID> active_controllers;

void players::update_controller_activity() {
    // Half deflection, so resting sticks and idle triggers never read as activity.
    constexpr Sint16 axis_threshold = 16384;
    std::unordered_set<SDL_JoystickID> active;
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(id);
        if (controller == nullptr) {
            continue;
        }

        bool pressed = false;
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX && !pressed; button++) {
            pressed = SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button)) != 0;
        }
        for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX && !pressed; axis++) {
            const Sint16 value = SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
            pressed = value > axis_threshold || value < -axis_threshold;
        }
        if (pressed) {
            active.insert(id);
        }
    }

    std::lock_guard lock(controller_activity_mutex);
    active_controllers = std::move(active);
}

int players::get_controller_number(SDL_GameController* controller) {
    if (controller == nullptr) {
        return 0;
    }
    const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    int number = 0;
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        if (!SDL_IsGameController(index)) {
            continue;
        }
        number++;
        if (SDL_JoystickGetDeviceInstanceID(index) == id) {
            return number;
        }
    }
    return 0;
}

bool players::is_controller_active(SDL_GameController* controller) {
    if (controller == nullptr) {
        return false;
    }
    const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    std::lock_guard lock(controller_activity_mutex);
    return active_controllers.count(id) != 0;
}

std::chrono::steady_clock::duration playerassignment::get_player_time_since_last_button_press(int player_index) {
    if (!PlayerState.temp_players.player_is_assigned(player_index)) {
        return std::chrono::steady_clock::duration::zero();
    }
    return ultramodern::time_since_start() - PlayerState.temp_players[player_index].last_button_press_timestamp;
}

void playerassignment::process_sdl_event(SDL_Event* event) {
    if (PlayerState.queue_close_player_assignment_modal) {
        recompui::AssignPlayersModal::close();
        PlayerState.queue_close_player_assignment_modal = false;
        if (recompui::controls_page != nullptr) {
            recompui::controls_page->force_update();
        }
        return;
    }

    if (!playerassignment::is_active()) {
        return;
    }

    switch (event->type) {
    case SDL_EventType::SDL_KEYDOWN: {
        SDL_KeyboardEvent* keyevent = &event->key;

        switch (keyevent->keysym.scancode) {
        case SDL_Scancode::SDL_SCANCODE_ESCAPE:
            // Cancel: edits live in temp_players, so the committed players are already intact.
            playerassignment::stop_and_close_modal();
            return;
        case SDL_Scancode::SDL_SCANCODE_RIGHT:
        case SDL_Scancode::SDL_SCANCODE_LEFT:
        case SDL_Scancode::SDL_SCANCODE_UP:
        case SDL_Scancode::SDL_SCANCODE_DOWN:
        // Menu navigation keys operate the dialog's buttons; they must not also join the keyboard.
        case SDL_Scancode::SDL_SCANCODE_RETURN:
        case SDL_Scancode::SDL_SCANCODE_KP_ENTER:
        case SDL_Scancode::SDL_SCANCODE_SPACE:
        case SDL_Scancode::SDL_SCANCODE_TAB:
        case SDL_Scancode::SDL_SCANCODE_BACKSPACE:
            break;
        default:
            if (playerassignment::was_keyboard_assigned()) {
                for (size_t i = 0; i < PlayerState.temp_players.size(); i++) {
                    if (PlayerState.temp_players[i].keyboard_enabled && PlayerState.temp_players[i].controller == nullptr) {
                        PlayerState.temp_players[i].button_pressed();
                    }
                }
            } else {
                playerassignment::add_keyboard_player();
            }
            break;
        }
        break;
    }
    case SDL_EventType::SDL_CONTROLLERBUTTONDOWN: {
        SDL_ControllerButtonEvent* button_event = &event->cbutton;
        SDL_JoystickID joystick_id = button_event->which;
        auto controller = recompinput::get_controller_from_joystick_id(joystick_id);

        bool can_be_mapped = true;
        for (size_t i = 0; i < PlayerState.temp_players.size(); i++) {
            if (PlayerState.temp_players[i].controller == controller) {
                can_be_mapped = false;
                PlayerState.temp_players[i].button_pressed();
                break;
            }
        }

        const int slot = playerassignment::get_next_free_slot();
        if (can_be_mapped && controller != nullptr && slot >= 0) {
            Player player{};
            player.controller = controller;
            player.button_pressed();
            PlayerState.temp_players.assign_slot(size_t(slot), player);
            record_join(slot);
        }

        break;
    }
    }
}
// player assignment end

}
