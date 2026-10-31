#pragma once

#include "recompinput/n64_adapter.h"
#include "recompinput/profiles.h"
#include <string>
#include <vector>

namespace bar::frontend {

// Manufacturer firmware mappings.c: USB buttons 0..13 are A, B, Z, Start,
// L, R, C-up/down/left/right, D-pad up/down/left/right. The stick is X/Y.
// Bind every C button to its own SDL button, avoiding synthesized opposing axes.
inline std::string n64_adapter_mapping(SDL_JoystickGUID guid) {
    char text[33]{};
    SDL_JoystickGetGUIDString(guid, text, sizeof(text));
    return std::string(text) + ",BAR raphnet Dual N64,"
        "a:b0,b:b1,lefttrigger:b2,start:b3,leftshoulder:b4,rightshoulder:b5,"
        "y:b6,x:b7,leftstick:b8,rightstick:b9,dpup:b10,dpdown:b11,"
        "dpleft:b12,dpright:b13,leftx:a0,lefty:a1,";
}

// Called before recompinput drains DEVICEADDED events. Never replace a mapping
// already supplied by SDL, SDL_GAMECONTROLLERCONFIG, or the player's mapping file.
// SDL only announces a device as a controller (SDL_CONTROLLERDEVICEADDED) if it already has a
// mapping when it is enumerated. Adapters present at launch are enumerated before this runs, so
// post that event for each port the new mapping turns into a controller; recompinput then creates
// its input profile exactly as for a hotplugged pad. Without it those pads have no profile, and
// the game reads them as unbound.
inline int register_n64_adapter_mappings() {
    // Both ports of an adapter share a GUID, so one mapping converts both: list them all first.
    std::vector<int> unmapped;
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (recompinput::is_raphnet_dual_n64(SDL_JoystickGetDeviceVendor(index), SDL_JoystickGetDeviceProduct(index)) &&
            !SDL_IsGameController(index)) {
            unmapped.push_back(index);
        }
    }
    int added = 0;
    for (int index : unmapped) {
        if (SDL_IsGameController(index)) continue;
        if (SDL_GameControllerAddMapping(n64_adapter_mapping(SDL_JoystickGetDeviceGUID(index)).c_str()) >= 0)
            ++added;
    }
    for (int index : unmapped) {
        if (!SDL_IsGameController(index)) continue;
        SDL_Event event{};
        event.type = SDL_CONTROLLERDEVICEADDED;
        event.cdevice.which = index;
        SDL_PushEvent(&event);
    }
    return added;
}

// Default overrides apply only to this verified layout. External SDL mappings
// remain authoritative, even when their device has the same vendor/product ID.
inline bool has_n64_adapter_mapping(SDL_GameController* controller) {
    if (!recompinput::is_raphnet_dual_n64(controller)) return false;
    const struct { SDL_GameControllerButton button; int physical; } bindings[] = {
        {SDL_CONTROLLER_BUTTON_A, 0}, {SDL_CONTROLLER_BUTTON_B, 1},
        {SDL_CONTROLLER_BUTTON_START, 3}, {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 4},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 5}, {SDL_CONTROLLER_BUTTON_Y, 6},
        {SDL_CONTROLLER_BUTTON_X, 7}, {SDL_CONTROLLER_BUTTON_LEFTSTICK, 8},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, 9}, {SDL_CONTROLLER_BUTTON_DPAD_UP, 10},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 11}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 12},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 13},
    };
    for (const auto& binding : bindings) {
        const auto actual = SDL_GameControllerGetBindForButton(controller, binding.button);
        if (actual.bindType != SDL_CONTROLLER_BINDTYPE_BUTTON || actual.value.button != binding.physical) return false;
    }
    const auto trigger = SDL_GameControllerGetBindForAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const auto x = SDL_GameControllerGetBindForAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const auto y = SDL_GameControllerGetBindForAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    return trigger.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && trigger.value.button == 2 &&
        x.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && x.value.axis == 0 &&
        y.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && y.value.axis == 1;
}

inline bool n64_adapter_default_mapping(SDL_GameController* controller, recompinput::GameInput input,
                                         std::vector<recompinput::InputField>& fields) {
    if (!has_n64_adapter_mapping(controller)) return false;
    using recompinput::GameInput;
    SDL_GameControllerButton button;
    switch (input) {
    case GameInput::C_UP: button = SDL_CONTROLLER_BUTTON_Y; break;
    case GameInput::C_DOWN: button = SDL_CONTROLLER_BUTTON_X; break;
    case GameInput::C_LEFT: button = SDL_CONTROLLER_BUTTON_LEFTSTICK; break;
    case GameInput::C_RIGHT: button = SDL_CONTROLLER_BUTTON_RIGHTSTICK; break;
    case GameInput::BACK_MENU: button = SDL_CONTROLLER_BUTTON_B; break;
    default: return false;
    }
    fields = {recompinput::InputField::controller_digital(button)};
    return true;
}

inline void initialize_n64_adapter_profile(SDL_GameController* controller, int profile) {
    for (int i = 0; i < static_cast<int>(recompinput::GameInput::COUNT); ++i) {
        const auto input = static_cast<recompinput::GameInput>(i);
        std::vector<recompinput::InputField> fields;
        if (!n64_adapter_default_mapping(controller, input, fields)) continue;
        recompinput::profiles::clear_input_binding(profile, input);
        for (size_t j = 0; j < fields.size(); ++j)
            recompinput::profiles::set_input_binding(profile, input, j, fields[j]);
    }
}

// Track the edge even while disallowed: regaining focus while holding the chord
// must not unexpectedly open the menu. Releasing and pressing again is required.
inline bool n64_menu_chord_edge(bool held, bool allowed, bool& previously_held) {
    const bool open = held && !previously_held && allowed;
    previously_held = held;
    return open;
}
}
