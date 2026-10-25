#pragma once
#include <SDL.h>

namespace RmlSDL {
// RmlUi's text editor uses its Ctrl flag for selection and clipboard shortcuts.
// SDL reports the Mac Command key as GUI; keep actual game bindings untouched.
constexpr bool UsesTextShortcutModifier(SDL_Keymod modifiers, bool macos) {
    return (modifiers & KMOD_CTRL) != 0 || (macos && (modifiers & KMOD_GUI) != 0);
}
} // namespace RmlSDL
