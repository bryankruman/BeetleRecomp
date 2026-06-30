// bar_config.cpp — BeetleRecomp feature/settings flags.
//
// These are the toggles the recompiled game code (RecompiledFuncs/*.c) queries at runtime. For now they
// are hardcoded / env-driven; the in-game settings menu being built on the feature/settings-menu-and-high-fps
// branch will replace these with persisted setting values. Keep the accessor names/signatures stable so the
// menu can just set the backing storage. All accessors are `extern "C"` so the C recompiled output can call them.
#include <cstdlib>

// Intro skip: let the player skip the attract cinematics (and, when wired, the legal/logo screens) by
// pressing A / B / Start, instead of waiting out their fixed display timers. The game already skips the
// attract on START/A; our hook adds B and (TODO) extends skipping to the boot legal screens. ON by default;
// set BAR_NO_INTRO_SKIP to keep intros unskippable. The controller-pak/save prompt is intentionally NOT
// skippable. TODO(settings-menu): back this with a persisted "Skip intros" setting.
extern "C" int bar_intro_skip(void) {
    static const int on = (std::getenv("BAR_NO_INTRO_SKIP") == nullptr) ? 1 : 0;
    return on;
}
