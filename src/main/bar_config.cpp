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

// Frame delta. func_uvgfxmgr_rom_00401004 returns the seconds since the previous frame, measured on the
// wall clock (uvClkGetSec), and the game steps its simulation and UV timelines by it
// (`step -= rate * delta`, uvtseq_rom.c:243). On the console that is a steady 1/30 or 1/20 s; on a PC a
// host hitch -- shader compilation, a stalled present, a window drag -- hands the game one long step,
// which the car physics, tuned for short steady steps, does not survive gracefully. Long steps are
// bounded to BAR_MAX_FRAME_DT seconds (default 0.1, i.e. 10 fps, below anything the console ran; 0
// disables), so a hitch slows the game for a frame instead of jumping it forward.
//
// scripts/fix-recompiled.sh renames the generated body to func_uvgfxmgr_rom_00401004__frame_delta_orig
// so this file owns the symbol.
//   BAR_TIME_SCALE=<float>  scale the delta (diagnostic; 1.0 is stock).
//   BAR_DBG_TIME=1          once a second, log the delta range and how many steps were bounded; and log
//                           each step longer than 50 ms as it happens, to line hitches up with gameplay.
#include <cstdio>
#include <chrono>
#include "recomp.h"

extern "C" float bar_timebase(float v) {
    static const float scale = [] {
        const char* s = std::getenv("BAR_TIME_SCALE");
        const float f = (s != nullptr) ? (float)std::atof(s) : 1.0f;
        return (f > 0.0f && f < 100.0f) ? f : 1.0f;
    }();
    static const float max_step = [] {
        const char* s = std::getenv("BAR_MAX_FRAME_DT");
        const float f = (s != nullptr) ? (float)std::atof(s) : 0.1f;
        return (f >= 0.0f && f < 10.0f) ? f : 0.1f;
    }();
    static const bool dbg = std::getenv("BAR_DBG_TIME") != nullptr;

    const float scaled = v * scale;
    const bool bounded = max_step > 0.0f && scaled > max_step;
    const float result = bounded ? max_step : scaled;

    if (dbg) {
        using clock = std::chrono::steady_clock;
        static const auto start = clock::now();
        static auto window = clock::now();
        static unsigned calls = 0, bounded_calls = 0;
        static float lo = 1e9f, hi = 0.0f, last_spike = 0.0f;
        const auto now = clock::now();
        const double t = std::chrono::duration<double>(now - start).count();
        calls++;
        bounded_calls += bounded ? 1 : 0;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        // The function is called several times per frame with the same value; report each spike once.
        if (v > 0.05f && v != last_spike) {
            std::fprintf(stderr, "[BAR_DBG_TIME] t=%.2fs long frame %.1f ms%s\n", t, v * 1000.0f,
                bounded ? " (bounded)" : "");
            last_spike = v;
        }
        if (now - window >= std::chrono::seconds(1)) {
            std::fprintf(stderr, "[BAR_DBG_TIME] t=%.2fs calls=%u delta min=%.1f ms max=%.1f ms bounded=%u\n",
                t, calls, lo * 1000.0f, hi * 1000.0f, bounded_calls);
            window = now;
            calls = bounded_calls = 0;
            lo = 1e9f;
            hi = 0.0f;
        }
    }
    return result;
}

extern "C" void func_uvgfxmgr_rom_00401004__frame_delta_orig(uint8_t* rdram, recomp_context* ctx);

extern "C" void func_uvgfxmgr_rom_00401004(uint8_t* rdram, recomp_context* ctx) {
    func_uvgfxmgr_rom_00401004__frame_delta_orig(rdram, ctx);
    ctx->f0.fl = bar_timebase(ctx->f0.fl);
}
