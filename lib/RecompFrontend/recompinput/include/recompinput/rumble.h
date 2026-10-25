#pragma once

#include <SDL.h>

namespace recompinput {
// Call from the input pump. Native SDL rumble remains the first choice; the
// raphnet dual-N64 adapter can fall back to its USB PID constant-force effect.
bool send_controller_rumble(SDL_GameController* controller, Uint16 low, Uint16 high, Uint32 duration_ms);
// Haptics must be closed before the associated joystick. Instance IDs, rather
// than device names or indices, keep the two adapter ports independent.
void release_controller_rumble(SDL_JoystickID instance_id);
void release_all_controller_rumble();
}
