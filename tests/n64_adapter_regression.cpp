#define SDL_MAIN_HANDLED
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "frontend/n64_adapter.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

static std::array<recompinput::InputField, recompinput::num_game_inputs> saved{};
namespace recompinput::profiles {
void clear_input_binding(int, GameInput input) { saved.at(static_cast<size_t>(input)) = {}; }
void set_input_binding(int, GameInput input, size_t, InputField field) { saved.at(static_cast<size_t>(input)) = field; }
}
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static int attach(Uint16 vendor, Uint16 product, const char* name) {
    SDL_VirtualJoystickDesc descriptor{};
    descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    descriptor.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    descriptor.naxes = 6;
    descriptor.nbuttons = 16;
    descriptor.vendor_id = vendor;
    descriptor.product_id = product;
    descriptor.name = name;
    return SDL_JoystickAttachVirtualEx(&descriptor);
}
int main() {
    try {
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        require(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0, "SDL initialization failed");
        require(!recompinput::is_raphnet_dual_n64(0x289b, 0x0094), "future firmware must remain unverified");
        require(!recompinput::is_raphnet_dual_n64(0x9999, 0x0064), "other vendor must not match");
        for (Uint16 product : {0x0030, 0x0036, 0x003c, 0x0064}) {
            const int first = attach(0x289b, product, "Dual N64 regression fixture");
            const int second = attach(0x289b, product, "Dual N64 regression fixture");
            require(first >= 0 && second >= 0, "virtual joystick attachment failed");
            // Unknown virtual joysticks exercise the same discovery path as a real
            // unmapped adapter; the second port shares its controller GUID.
            const auto mapping = bar::frontend::n64_adapter_mapping(SDL_JoystickGetDeviceGUID(first));
            require(bar::frontend::register_n64_adapter_mappings() > 0, "adapter discovery did not register a mapping");
            SDL_GameController* a = SDL_GameControllerOpen(first);
            SDL_GameController* b = SDL_GameControllerOpen(second);
            require(a && b && a != b, "both adapter ports must open independently");
            require(bar::frontend::has_n64_adapter_mapping(a), "canonical mapping not recognized");
            const std::array<SDL_GameControllerButton, 14> normalized = {
                SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_INVALID,
                SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_LEFTSTICK,
                SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
            auto* raw = SDL_GameControllerGetJoystick(a);
            for (int button = 0; button < 14; ++button) {
                require(SDL_JoystickSetVirtualButton(raw, button, 1) == 0, "virtual button set failed");
                SDL_GameControllerUpdate();
                if (button == 2) {
                    require(SDL_GameControllerGetAxis(a, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 32000, "Z trigger mapping failed");
                    require(SDL_GameControllerGetAxis(b, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0, "Z leaked to second port");
                } else {
                    require(SDL_GameControllerGetButton(a, normalized[button]) == 1, "physical N64 button mapping failed");
                    require(SDL_GameControllerGetButton(b, normalized[button]) == 0, "input leaked to second port");
                }
                SDL_JoystickSetVirtualButton(raw, button, 0);
                SDL_GameControllerUpdate();
            }
            // Opposing C buttons must stay independent, rather than cancelling on an axis.
            SDL_JoystickSetVirtualButton(raw, 6, 1);
            SDL_JoystickSetVirtualButton(raw, 7, 1);
            SDL_GameControllerUpdate();
            require(SDL_GameControllerGetButton(a, SDL_CONTROLLER_BUTTON_Y) && SDL_GameControllerGetButton(a, SDL_CONTROLLER_BUTTON_X), "opposing C buttons cancelled");
            SDL_JoystickSetVirtualAxis(raw, 0, 30000);
            SDL_JoystickSetVirtualAxis(raw, 1, -20000);
            SDL_GameControllerUpdate();
            require(SDL_GameControllerGetAxis(a, SDL_CONTROLLER_AXIS_LEFTX) == 30000 && SDL_GameControllerGetAxis(a, SDL_CONTROLLER_AXIS_LEFTY) == -20000, "stick axes/signs changed");
            bar::frontend::initialize_n64_adapter_profile(a, 1);
            using recompinput::GameInput;
            require(saved[static_cast<size_t>(GameInput::C_UP)].input_id == SDL_CONTROLLER_BUTTON_Y, "C-up profile default incorrect");
            require(saved[static_cast<size_t>(GameInput::C_RIGHT)].input_id == SDL_CONTROLLER_BUTTON_RIGHTSTICK, "C-right profile default incorrect");
            require(saved[static_cast<size_t>(GameInput::BACK_MENU)].input_id == SDL_CONTROLLER_BUTTON_B, "menu Back must use B");
            // A caller's SDL mapping must survive discovery and avoid incompatible
            // adapter-specific overrides when its physical layout differs.
            auto custom = mapping;
            custom.replace(custom.find("a:b0"), 4, "a:b1");
            require(SDL_GameControllerAddMapping(custom.c_str()) >= 0, "external mapping setup failed");
            bar::frontend::register_n64_adapter_mappings();
            require(SDL_GameControllerGetBindForButton(a, SDL_CONTROLLER_BUTTON_A).value.button == 1, "external mapping was overwritten");
            std::vector<recompinput::InputField> fields;
            require(!bar::frontend::n64_adapter_default_mapping(a, GameInput::C_UP, fields), "external layout must not receive guessed defaults");
            SDL_GameControllerClose(a);
            SDL_GameControllerClose(b);
            SDL_JoystickDetachVirtual(second);
            SDL_JoystickDetachVirtual(first);
        }
        bool previous = false;
        require(bar::frontend::n64_menu_chord_edge(true, true, previous), "initial chord did not open");
        require(!bar::frontend::n64_menu_chord_edge(true, true, previous), "held chord repeated");
        bar::frontend::n64_menu_chord_edge(false, false, previous);
        require(!bar::frontend::n64_menu_chord_edge(true, false, previous), "unfocused/binding chord opened");
        require(!bar::frontend::n64_menu_chord_edge(true, true, previous), "focus regain reopened held chord");
        bar::frontend::n64_menu_chord_edge(false, true, previous);
        require(bar::frontend::n64_menu_chord_edge(true, true, previous), "released chord did not rearm");
        SDL_Quit();
        std::cout << "PASS: verified adapter IDs, dual-port button/axis mapping, independent C buttons, defaults, external mappings and menu chord edges\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (SDL: " << SDL_GetError() << ")\n";
        SDL_Quit();
        return 1;
    }
}
