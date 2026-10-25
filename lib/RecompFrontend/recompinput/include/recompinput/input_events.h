#pragma once

#include "recompinput.h"
#include <functional>

namespace recompinput {
    // Applied only to newly discovered device profiles; saved bindings stay intact.
    void set_controller_profile_initializer(std::function<void(SDL_GameController*, int)> initializer);
    void handle_events();
    void purge_deferred_controller_profiles();
}
