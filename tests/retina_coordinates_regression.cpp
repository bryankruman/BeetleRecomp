#define SDL_MAIN_HANDLED
#include "base/ui_pixel_coordinates.h"
#include <cassert>
#include <cstdio>

int main() {
    using recompui::detail::event_to_pixels;
    using recompui::detail::coordinate_to_pixels;
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 125; event.motion.y = 63;
    event.motion.xrel = -3; event.motion.yrel = 2;
    event_to_pixels(event, 960, 720, 1920, 1440);
    assert(event.motion.x == 250 && event.motion.y == 126);
    assert(event.motion.xrel == -6 && event.motion.yrel == 4);
    for (auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
        event.type = type; event.button.x = 110; event.button.y = 32;
        event_to_pixels(event, 960, 720, 1920, 1440);
        assert(event.button.x == 220 && event.button.y == 64);
    }
    event.type = SDL_WINDOWEVENT; event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    event.window.data1 = 960; event.window.data2 = 720;
    event_to_pixels(event, 960, 720, 1920, 1440);
    assert(event.window.data1 == 1920 && event.window.data2 == 1440);
    event.type = SDL_MOUSEWHEEL; event.wheel.y = -1;
    event_to_pixels(event, 960, 720, 1920, 1440);
    assert(event.wheel.y == -1);
    assert(coordinate_to_pixels(123, 960, 960) == 123);
    assert(coordinate_to_pixels(100, 800, 1000) == 125);
    assert(coordinate_to_pixels(-100, 800, 1600) == -200);
    assert(coordinate_to_pixels(123, 0, 1920) == 123);
    assert(coordinate_to_pixels(123, 960, 0) == 123);
    assert(coordinate_to_pixels(2147483647, 1, 2) == 2147483647);
    std::puts("PASS: Retina motion/click/resize coordinates, fractional scaling, scroll and minimized-window guards");
}
