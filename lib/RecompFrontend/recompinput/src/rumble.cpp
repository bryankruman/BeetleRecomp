#include "recompinput/rumble.h"
#include "recompinput/n64_adapter.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace {
struct RumbleFallback {
    SDL_Haptic* haptic = nullptr;
    int effect = -1;
    bool owns_subsystem = false;
};
std::mutex fallback_mutex;
std::unordered_map<SDL_JoystickID, RumbleFallback> fallbacks;

void close_fallback(RumbleFallback& state) {
    if (state.haptic != nullptr) {
        if (state.effect >= 0) {
            SDL_HapticStopEffect(state.haptic, state.effect);
            SDL_HapticDestroyEffect(state.haptic, state.effect);
        }
        SDL_HapticClose(state.haptic);
        state.haptic = nullptr;
        state.effect = -1;
    }
    if (state.owns_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
        state.owns_subsystem = false;
    }
}

SDL_HapticEffect constant_effect(Uint32 duration_ms) {
    SDL_HapticEffect effect{};
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    // The adapter firmware implements a binary N64 motor and ignores small
    // force magnitudes. Its Pak is either on or off, unlike a modern gamepad.
    effect.constant.level = 32767;
    effect.constant.length = duration_ms;
    return effect;
}

bool play_fallback(RumbleFallback& state, bool on, Uint32 duration_ms) {
    if (state.haptic == nullptr) return false; // Do not retry a failed device every frame.
    if (!on) {
        if (SDL_HapticStopEffect(state.haptic, state.effect) == 0) return true;
    } else {
        SDL_HapticEffect effect = constant_effect(duration_ms);
        if (SDL_HapticUpdateEffect(state.haptic, state.effect, &effect) == 0 &&
            SDL_HapticRunEffect(state.haptic, state.effect, 1) == 0) return true;
    }
    close_fallback(state);
    return false;
}
}

bool recompinput::send_controller_rumble(SDL_GameController* controller, Uint16 low, Uint16 high, Uint32 duration_ms) {
    if (controller == nullptr) return false;
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    if (joystick == nullptr || SDL_JoystickGetAttached(joystick) != SDL_TRUE) return false;
    const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    if (id < 0) return false;
    const bool adapter = is_raphnet_dual_n64(SDL_JoystickGetVendor(joystick), SDL_JoystickGetProduct(joystick));
    const bool on = (low != 0 || high != 0) && duration_ms != 0;
    if (adapter) {
        low = high = on ? 65535 : 0;
        // Finite effects stop if the game or input pump stalls. The raw path
        // previously requested 1,000 seconds; adapters need no such timeout.
        duration_ms = on ? std::min<Uint32>(duration_ms, 250) : 0;
    }
    std::lock_guard lock(fallback_mutex);
    auto found = fallbacks.find(id);
    if (found != fallbacks.end()) return play_fallback(found->second, on, duration_ms);
    if (SDL_JoystickRumble(joystick, low, high, duration_ms) == 0) return true;
    if (!adapter) return false;
    if (!on) return true; // Nothing is running; do not open a force-feedback device to stop it.

    auto& state = fallbacks[id];
    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) return false;
    state.owns_subsystem = true;
    // Open from this exact joystick, never by enumeration/name. Both physical
    // ports can have identical names and serials but separate HID interfaces.
    state.haptic = SDL_HapticOpenFromJoystick(joystick);
    if (state.haptic == nullptr || (SDL_HapticQuery(state.haptic) & SDL_HAPTIC_CONSTANT) == 0) {
        close_fallback(state);
        return false;
    }
    SDL_HapticEffect effect = constant_effect(duration_ms);
    state.effect = SDL_HapticNewEffect(state.haptic, &effect);
    if (state.effect < 0) {
        close_fallback(state);
        return false;
    }
    return play_fallback(state, true, duration_ms);
}

void recompinput::release_controller_rumble(SDL_JoystickID instance_id) {
    std::lock_guard lock(fallback_mutex);
    auto found = fallbacks.find(instance_id);
    if (found != fallbacks.end()) {
        close_fallback(found->second);
        fallbacks.erase(found);
    }
}

void recompinput::release_all_controller_rumble() {
    std::lock_guard lock(fallback_mutex);
    for (auto& entry : fallbacks) close_fallback(entry.second);
    fallbacks.clear();
}
