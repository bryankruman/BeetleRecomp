#pragma once

#include "recompinput/input_mapping.h"

namespace bar::frontend {

// BAR's original Controls defaults, expressed in SDL's normalized controller layout.
// Register before profiles are loaded: saved bindings, including an explicitly empty
// binding, belong to the player and must not be replaced during device discovery.
inline void register_controller_defaults() {
    using recompinput::GameInput;
    using recompinput::InputField;
    const struct { GameInput input; InputField field; } bindings[] = {
        {GameInput::A, InputField::controller_digital(SDL_CONTROLLER_BUTTON_A)},
        {GameInput::B, InputField::controller_digital(SDL_CONTROLLER_BUTTON_B)},
        {GameInput::Z, InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERLEFT, true)},
        {GameInput::START, InputField::controller_digital(SDL_CONTROLLER_BUTTON_START)},
        {GameInput::L, InputField::controller_digital(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)},
        {GameInput::R, InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)},
        {GameInput::C_UP, InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTY, false)},
        {GameInput::C_DOWN, InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTY, true)},
        {GameInput::C_LEFT, InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTX, false)},
        {GameInput::C_RIGHT, InputField::controller_analog(SDL_CONTROLLER_AXIS_RIGHTX, true)},
        {GameInput::DPAD_UP, InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_UP)},
        {GameInput::DPAD_DOWN, InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN)},
        {GameInput::DPAD_LEFT, InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_LEFT)},
        {GameInput::DPAD_RIGHT, InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)},
        {GameInput::Y_AXIS_POS, InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTY, false)},
        {GameInput::Y_AXIS_NEG, InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTY, true)},
        {GameInput::X_AXIS_NEG, InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTX, false)},
        {GameInput::X_AXIS_POS, InputField::controller_analog(SDL_CONTROLLER_AXIS_LEFTX, true)},
    };
    for (const auto& binding : bindings) {
        recompinput::set_default_mapping_for_controller(binding.input, {binding.field});
    }
}

} // namespace bar::frontend
