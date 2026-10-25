#define SDL_MAIN_HANDLED
#include "recompinput/rumble.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
enum class Failure { None, Subsystem, Open, Query, Create, Update, Run, Stop };
struct Device {
    SDL_JoystickID id;
    Uint16 vendor = 0x289b, product = 0x0064;
    bool attached = true;
    int native_result = -1, native_calls = 0, opens = 0, closes = 0, creates = 0, destroys = 0, updates = 0, runs = 0, stops = 0;
    Uint16 low = 0, high = 0;
    Uint32 duration = 0;
    SDL_HapticEffect effect{};
    Failure failure = Failure::None;
};
int init_calls = 0, quit_calls = 0, owned_refs = 0;
bool init_failure = false;
Device& device(SDL_Joystick* pointer) { return *reinterpret_cast<Device*>(pointer); }
Device& device(SDL_Haptic* pointer) { return *reinterpret_cast<Device*>(pointer); }
SDL_GameController* controller(Device& value) { return reinterpret_cast<SDL_GameController*>(&value); }
void clean(Device& value) { recompinput::release_controller_rumble(value.id); }
}

extern "C" {
SDL_Joystick* SDLCALL SDL_GameControllerGetJoystick(SDL_GameController* value) { return reinterpret_cast<SDL_Joystick*>(value); }
SDL_bool SDLCALL SDL_JoystickGetAttached(SDL_Joystick* value) { return device(value).attached ? SDL_TRUE : SDL_FALSE; }
SDL_JoystickID SDLCALL SDL_JoystickInstanceID(SDL_Joystick* value) { return device(value).id; }
Uint16 SDLCALL SDL_JoystickGetVendor(SDL_Joystick* value) { return device(value).vendor; }
Uint16 SDLCALL SDL_JoystickGetProduct(SDL_Joystick* value) { return device(value).product; }
int SDLCALL SDL_JoystickRumble(SDL_Joystick* value, Uint16 low, Uint16 high, Uint32 duration) {
    auto& d = device(value); ++d.native_calls; d.low = low; d.high = high; d.duration = duration; return d.native_result;
}
int SDLCALL SDL_InitSubSystem(Uint32 flags) {
    require(flags == SDL_INIT_HAPTIC, "unexpected subsystem initialization"); ++init_calls;
    if (init_failure) return -1;
    ++owned_refs; return 0;
}
void SDLCALL SDL_QuitSubSystem(Uint32 flags) {
    require(flags == SDL_INIT_HAPTIC && owned_refs > 0, "unbalanced haptic subsystem release"); ++quit_calls; --owned_refs;
}
SDL_Haptic* SDLCALL SDL_HapticOpenFromJoystick(SDL_Joystick* value) {
    auto& d = device(value); ++d.opens;
    return d.failure == Failure::Open ? nullptr : reinterpret_cast<SDL_Haptic*>(value);
}
unsigned int SDLCALL SDL_HapticQuery(SDL_Haptic* value) { return device(value).failure == Failure::Query ? SDL_HAPTIC_SINE : SDL_HAPTIC_CONSTANT; }
int SDLCALL SDL_HapticNewEffect(SDL_Haptic* value, SDL_HapticEffect* effect) {
    auto& d = device(value); ++d.creates; d.effect = *effect; return d.failure == Failure::Create ? -1 : d.id;
}
int SDLCALL SDL_HapticUpdateEffect(SDL_Haptic* value, int id, SDL_HapticEffect* effect) {
    auto& d = device(value); require(id == d.id, "effect updated on wrong physical port"); ++d.updates; d.effect = *effect;
    return d.failure == Failure::Update ? -1 : 0;
}
int SDLCALL SDL_HapticRunEffect(SDL_Haptic* value, int id, Uint32 iterations) {
    auto& d = device(value); require(id == d.id && iterations == 1, "incorrect port or unbounded haptic effect"); ++d.runs;
    return d.failure == Failure::Run ? -1 : 0;
}
int SDLCALL SDL_HapticStopEffect(SDL_Haptic* value, int id) {
    auto& d = device(value); require(id == d.id, "effect stopped on wrong physical port"); ++d.stops;
    return d.failure == Failure::Stop ? -1 : 0;
}
void SDLCALL SDL_HapticDestroyEffect(SDL_Haptic* value, int id) {
    auto& d = device(value); require(id == d.id, "effect destroyed on wrong physical port"); ++d.destroys;
}
void SDLCALL SDL_HapticClose(SDL_Haptic* value) { ++device(value).closes; }
}

int main() {
    try {
        using recompinput::send_controller_rumble;
        require(!send_controller_rumble(nullptr, 1, 1, 1), "null controller accepted");
        Device ordinary{1}; ordinary.vendor = 0x045e; ordinary.native_result = 0;
        require(send_controller_rumble(controller(ordinary), 1234, 5678, 765), "native modern gamepad failed");
        require(ordinary.low == 1234 && ordinary.high == 5678 && ordinary.duration == 765 && init_calls == 0, "modern gamepad motor values changed");
        ordinary.native_result = -1;
        require(!send_controller_rumble(controller(ordinary), 1, 1, 250) && init_calls == 0, "unknown device must not use adapter workaround");
        Device native{2}; native.native_result = 0;
        require(send_controller_rumble(controller(native), 1, 0, 1000000), "native adapter rumble failed");
        require(native.low == 65535 && native.high == 65535 && native.duration == 250 && init_calls == 0, "adapter binary motor or bounded duration incorrect");
        require(send_controller_rumble(controller(native), 999, 999, 0) && native.low == 0 && native.high == 0, "zero-duration adapter rumble must stop");
        native.attached = false;
        require(!send_controller_rumble(controller(native), 1, 1, 250), "detached controller accepted");
        Device first{10}, second{11};
        require(send_controller_rumble(controller(first), 0, 0, 0) && first.opens == 0, "stop should not open a new effect");
        require(send_controller_rumble(controller(first), 1, 1, 1000000), "constant fallback failed");
        require(first.effect.type == SDL_HAPTIC_CONSTANT && first.effect.constant.level == 32767 && first.effect.constant.length == 250, "constant effect content incorrect");
        require(send_controller_rumble(controller(second), 600, 0, 175), "second physical port failed");
        require(second.effect.constant.length == 175 && owned_refs == 2, "separate port ownership incorrect");
        const int native_before = first.native_calls;
        require(send_controller_rumble(controller(first), 1, 1, 100), "existing effect refresh failed");
        require(first.opens == 1 && first.creates == 1 && first.native_calls == native_before, "refresh recreated or reset effect");
        require(send_controller_rumble(controller(first), 0, 0, 0) && first.stops == 1 && second.stops == 0, "stopping port one disturbed port two");
        clean(first);
        require(first.closes == 1 && first.destroys == 1 && owned_refs == 1 && second.closes == 0, "disconnect cleanup disturbed other port");
        clean(first);
        require(first.closes == 1, "cleanup must be idempotent");
        recompinput::release_all_controller_rumble();
        require(second.closes == 1 && second.destroys == 1 && owned_refs == 0, "shutdown cleanup leaked effects");
        int next_id = 20;
        for (Failure failure : {Failure::Subsystem, Failure::Open, Failure::Query, Failure::Create, Failure::Update, Failure::Run}) {
            Device d{next_id++}; d.failure = failure; init_failure = failure == Failure::Subsystem;
            require(!send_controller_rumble(controller(d), 10, 10, 250), "injected failure unexpectedly passed");
            require(owned_refs == 0, "failure leaked owned haptic subsystem reference");
            const int init_before = init_calls, open_before = d.opens;
            require(!send_controller_rumble(controller(d), 10, 10, 250), "failed device unexpectedly retried");
            require(init_calls == init_before && d.opens == open_before, "failed device retried every frame");
            clean(d);
            init_failure = false; d.failure = Failure::None;
            require(send_controller_rumble(controller(d), 10, 10, 250), "reconnected device cannot retry");
            clean(d);
            require(owned_refs == 0, "reconnect leaked subsystem reference");
        }
        Device stop_failure{99};
        require(send_controller_rumble(controller(stop_failure), 1, 1, 250), "stop-failure fixture could not start");
        stop_failure.failure = Failure::Stop;
        require(!send_controller_rumble(controller(stop_failure), 0, 0, 0), "stop failure must report failure");
        require(stop_failure.closes == 1 && stop_failure.destroys == 1 && owned_refs == 0, "stop failure leaked resources");
        clean(stop_failure);
        std::cout << "PASS: native rumble, adapter binary output, exact-port constant fallback, finite effects, failure cleanup, and reconnect retry\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
