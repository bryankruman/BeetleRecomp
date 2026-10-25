#pragma once
#include <SDL.h>

namespace recompinput {
// raphnet ADAP-2XN64-USB, verified firmware 3.3 through 3.6.
// Do not match other raphnet adapters or unverified future firmware layouts.
inline bool is_raphnet_dual_n64(Uint16 vendor, Uint16 product) {
    return vendor == 0x289b && (product == 0x0030 || product == 0x0036 ||
        product == 0x003c || product == 0x0064);
}
inline bool is_raphnet_dual_n64(SDL_GameController* controller) {
    return controller && is_raphnet_dual_n64(SDL_GameControllerGetVendor(controller),
                                            SDL_GameControllerGetProduct(controller));
}
}
