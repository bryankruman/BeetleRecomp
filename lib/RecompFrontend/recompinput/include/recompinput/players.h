#ifndef __RECOMP_INPUT_PLAYER_ASSIGNMENT_H__
#define __RECOMP_INPUT_PLAYER_ASSIGNMENT_H__

#include "recompinput.h"

namespace recompinput {
    struct Player {
        SDL_GameController* controller = nullptr;
        bool keyboard_enabled = false;
        std::chrono::high_resolution_clock::duration last_button_press_timestamp = std::chrono::high_resolution_clock::duration::zero();

        Player() : controller(nullptr), keyboard_enabled(false), last_button_press_timestamp(std::chrono::high_resolution_clock::duration::zero()) {};

        void button_pressed();
    };

    namespace players {
        size_t get_number_of_assigned_players();
        size_t get_max_number_of_players();
        bool is_single_player_mode();
        void set_single_player_mode(bool single_player);
        void set_min_number_of_players(size_t min_players);
        void set_max_number_of_players(size_t max_players);
        void set_player_count_range(size_t min_players, size_t max_players);
        const Player& get_player(int player_index, bool temp_player = false);
        bool get_player_is_assigned(int player_index, bool temp_player = false);
        bool has_enough_players_assigned();
        InputDevice get_player_input_device(int player_index, bool temp_player = false);

        // Assigns the given controllers to players in order, up to the maximum
        // number of players, as though they had been assigned through the modal
        // and committed. Passing none assigns the keyboard to player one, so a
        // machine with no pad attached still has a player. Does nothing while a
        // manual assignment is open.
        void auto_assign_controllers(SDL_GameController* const* controllers, size_t count);
        // Preserve explicit console slots, including unassigned gaps and imported
        // per-player keyboard profiles. Manual assignment takes precedence.
        void auto_assign_players(const Player* slots, size_t count);

        // Live "is this pad being used" state for the UI, so players can tell their controllers apart.
        // Refresh once per tick on the SDL event thread; query from any thread.
        void update_controller_activity();
        bool is_controller_active(SDL_GameController* controller);
        // 1-based position of the pad among connected controllers ("Pad 2"), or 0 if not connected.
        int get_controller_number(SDL_GameController* controller);
    }

    namespace playerassignment {
        bool is_active();
        // Starts with every slot empty.
        void start();
        // Starts from the committed players so they can be edited rather than re-entered.
        void start_from_current();
        // Players in the session; slots may be empty, so this is not the highest slot number.
        size_t get_assigned_count();
        // The slot the next joining device fills, or -1 when full or inactive.
        int get_next_free_slot();
        void remove_player(int player_index);
        bool can_undo();
        void undo_last_join();
        // Swaps two slots, either of which may be empty (moving a player into a gap).
        void swap_players(int first, int second);
        void stop();
        void stop_and_close_modal();
        void commit_player_assignment();
        void add_keyboard_player();
        bool met_assignment_requirements();
        bool is_blocking_input();
        bool is_player_currently_assigning(int player_index);
        bool was_keyboard_assigned();
        std::chrono::steady_clock::duration get_player_time_since_last_button_press(int player_index);

        void process_sdl_event(SDL_Event* event);
    }
}

#endif // __RECOMP_INPUT_PLAYER_ASSIGNMENT_H__
