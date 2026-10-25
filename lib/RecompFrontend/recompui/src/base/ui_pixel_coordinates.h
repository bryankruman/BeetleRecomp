#pragma once

#include <SDL_events.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace recompui::detail {
inline int coordinate_to_pixels(int value, int logical_extent, int pixel_extent) {
    if (logical_extent <= 0 || pixel_extent <= 0) return value;
    const double scaled = std::round(double(value) * pixel_extent / logical_extent);
    return static_cast<int>(std::clamp(scaled, double(std::numeric_limits<int>::min()),
                                      double(std::numeric_limits<int>::max())));
}

// Operates on a copy: gameplay keeps SDL's original coordinates and scroll distances.
inline void event_to_pixels(SDL_Event& event, int width, int height, int pixels_w, int pixels_h) {
    switch (event.type) {
    case SDL_MOUSEMOTION:
        event.motion.x = coordinate_to_pixels(event.motion.x, width, pixels_w);
        event.motion.y = coordinate_to_pixels(event.motion.y, height, pixels_h);
        event.motion.xrel = coordinate_to_pixels(event.motion.xrel, width, pixels_w);
        event.motion.yrel = coordinate_to_pixels(event.motion.yrel, height, pixels_h);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        event.button.x = coordinate_to_pixels(event.button.x, width, pixels_w);
        event.button.y = coordinate_to_pixels(event.button.y, height, pixels_h);
        break;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && pixels_w > 0 && pixels_h > 0) {
            event.window.data1 = pixels_w;
            event.window.data2 = pixels_h;
        }
        break;
    default: break;
    }
}
}
