# Project Status & Resume Guide

_Last updated: 2026-06-30_

## TL;DR
- ✅ **Builds + links + runs** → `build-cmake/BeetleRecomp.exe` (native Windows, clang-cl, RT64/D3D12).
- ✅ **Plays** — boots through the intros/menus into races, **renders at 60 fps**, **keyboard input**
  works, and **audio plays**. The boot/menu pipeline (uv module overlay bridge, 78 modules, zero
  "Failed to find function"), VI, SI, RSP gfx + audio tasks all run.
- ✅ **Black screen FIXED** — it was a controller/SI `osRecvMesg` deadlock, not a gfx problem. Renders
  60 fps now. (Commit `b92a2cc`.)
- ✅ **Menu 60 fps** — was ~10 fps from an SI-queue requeue busy-spin; `requeue_si=false` (main.cpp:507).
- ✅ **Audio working** — RSP `aspMain` ucode wired; correct PCM transform (no-byteswap + channel-swap);
  latency + transition-crackle fixed (see WIP item 2 below).
- ✅ **Coventry Cove crash FIXED** — heap OOM; heap cap extended 4 MB → 8 MB (`fix-recompiled.sh` rule A).
- 🚧 **Current focus: polish.** Four items raised 2026-06-30 — see **[Current WIP](#current-wip--polish-items-2026-06-30)**.
  Audio (item 2) is done; legal-screen skip, menu-transition flash, and track-map fps remain.

## Current WIP — polish items (2026-06-30)

Four issues were raised together; each was investigated with a verified multi-agent workflow. Status:

### 1. Legal/logo screen skip — 🚧 TODO (attract part done)
**Goal:** make the boot legal screens (Licensed by Nintendo, Electronic Arts, trademark/legal) **and**
the attract cinematics skippable by pressing **A / B / or Start** — *except* the first controller-pak /
"continue without saving" prompt, which must stay non-skippable.
- ✅ **Attract cinematics** already skip on A/B/Start: the game skips on START/A in
  `func_intro_004005CC`; we inject a B-button check after the `0x81D00618` input read
  (`fix-recompiled.sh` **rule D**, gated by `bar_intro_skip()` in `src/main/bar_config.cpp`).
- ❌ **Legal/logo screens NOT skippable yet.** They are driven by **un-decompiled boot assembly** whose
  per-frame handler hasn't been pinned down (`func_selection_004000E4`'s sub-handler is null during boot;
  the `logo` UVBT module didn't load in the boot trace; the legal text is likely uvFont-rendered by an
  un-found handler on a `uvClkGetSec` timer). The workflow agent for this item **failed** (hit the
  structured-output retry cap), so there is no lead yet.
- **Next:** recreate the in-process stack sampler (see memory `debug-build-run-loop`) to catch the active
  `func_*` during the EA/Nintendo screen, **or** instrument the 'game' module per-frame dispatch
  (`gGameExports->unk4`) to find the boot-screen driver, then add an A/B/Start check to its timer-advance.
  Keep the controller-pak prompt non-skippable.

### 2. Audio latency + clipping — ✅ DONE
- **Ever-growing latency** (audio played seconds behind the game): `osAiGetLength()` (`func_8000E460`,
  `src/main/hw_stubs.cpp`) was hard-stubbed to `0`, so the audio manager always thought the AI buffer was
  empty and synthesized the **max** samples every frame, flooding the unbounded SDL queue. **Fixed:** it
  now returns `ultramodern::get_remaining_audio_bytes()`. Queue is bounded; latency is small + constant.
- **Transition crackle** (underrun on every menu/state change): `func_uvcmidi_rom_00400940` stops the MIDI
  sequence player then **spins up to its 2.0 s timeout** waiting for the player to report stopped — but the
  player state only advances when the audio thread runs, which the cooperative scheduler couldn't do mid-
  spin. So it burned the full 2 s, starving audio. **Fixed:** inject `yield_self_1ms` into that loop
  (`fix-recompiled.sh` **rule E**).
- **General audio-thread starvation** during long game-thread compute (no preemption in the cooperative
  scheduler): **cooperative preemption** — a ~500 Hz host timer (`src/main/bar_preempt.cpp`) raises a
  "yield" flag; a one-line poll injected at every recompiled-function prologue (`fix-recompiled.sh`
  **rule F**) yields the App thread to the audio manager via `yield_self_1ms`. Gated to the **low-priority
  App thread only** (`ultramodern::this_thread_priority() < 100`) — yielding on the audio/scheduler threads
  themselves silenced audio and crashed; the priority gate fixes that. N64-correct; stable.
- **Buffer floor:** `buffer_offset_frames` default **2.5** (`ultramodern/src/audio.cpp`); ~1 VI (1.0) is too
  thin for glitch-free *desktop* audio (host OS + SDL device-callback jitter dip the queue regardless of the
  game-side fixes). Live-tunable with **`BAR_AUDIO_BUFFER`** (e.g. `=1.8`) to trade latency vs crackle.
- _Possible future latency win (untried): shrink the SDL device buffer (`want.samples`, currently 1024 ≈
  46 ms) in `bar_open_audio` — a separate latency lever from the queue offset._

### 3. Menu page-transition background flash — 🚧 TODO (workflow fix REFUTED)
**Symptom:** a one-frame flash in the background each time the menu slides to the next page.
- ⚠️ The workflow's proposed fix is **wrong — do NOT ship it.** Both adversarial reviewers refuted it: it
  rests on `func_selection_00418800` reading a UvGfxMgr export struct at `0x80025C74`, but that address is
  `gGameGuiExports` (the real gfx-mgr struct is `0x80025C08`). The recommended `func_uvgfxmgr_rom_00400F58`
  edit is outside the transition path and would risk a regression.
- **Directionally likely (unproven):** RT64 is HLE and reconstructs framebuffer-as-texture by RDRAM address;
  a 1-frame flash when the slide samples a just-written FB with **no tracked RT64 target yet** is a plausible
  class of bug.
- **Next:** re-decode `func_selection_00418800` (`funcs_5.c:2226-2312`) against the **real** callees
  (`gUvGuiExports->+0x70/74/78` = `uvgui_rom`; `gGameGuiExports->+0x14` = gamegui) to find which RDRAM
  address is captured as the slide source; capture (RT64 debug / RenderDoc) whether the slide's first frame
  samples an FB address with no tracked target. If so, the low-risk fix is RT64-side: in
  `lib/rt64/.../rt64_framebuffer_manager.cpp`, clear the reconstructed tile to transparent when there is
  genuinely no tracked target for the requested address (never when one exists — preserve real FB feedback).

### 4. Track-map preview animates choppy/low-fps — 🚧 TODO (needs one live trace)
**Symptom:** the spinning 3D track-map preview on the map-select screen animates at a low/glitchy frame rate
while the rest of the menu is smooth 60 fps.
- **Proven negatives (do not re-litigate):** it is **not** the whole-frame divider (`D_8001F7C0`,
  `funcs_45.c`), **not** renderer interpolation (off — `RefreshRate::Original`), and the time base
  `func_uvgfxmgr_rom_00401004()` is correct. The map-update site is in `selection.c` (100% GLOBAL_ASM),
  so the positive cause can't be read statically. Do NOT touch `speed_multiplier` / the VI clock.
- **Next (one decisive live experiment):** log per VI frame — (a) the map rotation angle, (b)
  `func_uvgfxmgr_rom_00401004()` real sec/frame, (c) a present-cadence timestamp; also read
  `D_uvgfxmgr_rom_00402480`. Classify → **Fix A** scale the per-tick step by real delta (idiom at
  `uvtseq_rom.c:243`, most likely), **Fix B** ungate an every-N update, **Fix C** drive the keyframe phase
  continuously, or it's a gfx-completion present-cadence stall. The fix lands in `RecompiledFuncs/` → it
  **must** be encoded as an address-keyed `fix-recompiled.sh` rule.

## What works
The full static-recompilation pipeline compiles end to end:

```
your ROM → BeetleDecomp `make recomp` → recomp.elf (133 module overlays)
        → N64Recomp → 22,406 functions (RecompiledFuncs/)
        → clang-cl + RT64 + librecomp/ultramodern → BeetleRecomp.exe
```

## Verified build environment (Windows)
- **VS Build Tools 2022** — `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
  (MSVC 14.42 + Win SDK 10.0.22621/26100). NB: the `C:\Program Files\…\BuildTools` dir is an empty shell.
- **LLVM clang-cl 22** — `C:\Program Files\LLVM\bin\clang-cl.exe` (`winget install LLVM.LLVM`).
- **CLion** — `%LOCALAPPDATA%\Programs\CLion` (its bundled CMake + Ninja are used).
- **N64Recomp** — built in WSL at `~/tools/N64Recomp`, checked out at **`ffb39cd`** (NOT `81213c1`).

## Reproduce the build (from a clean tree)
```bash
# 0. Dependencies (once)
git submodule update --init --recursive                 # in BeetleRecomp (RT64 contrib, etc.)
#    Install: VS Build Tools 2022 (Desktop C++ + Win SDK), LLVM (clang-cl), CLion.

# 1. Decomp -> per-module ELF (in WSL)
cd ~/projects/bar-decomp && source .venv/bin/activate && make recomp

# 2. Bring the ELF into the recomp repo
scripts/fetch-elf.sh                                     # -> elf/recomp.elf

# 3. Build N64Recomp @ ffb39cd (WSL) and recompile to C
#    git clone https://github.com/N64Recomp/N64Recomp ~/tools/N64Recomp
#    cd ~/tools/N64Recomp && git checkout ffb39cd && git submodule update --init --recursive
#    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j6
~/tools/N64Recomp/build/N64Recomp BeetleRecomp.toml      # -> RecompiledFuncs/

# 4. REQUIRED post-gen fixup (the `lw $zero` codegen quirk)
scripts/fix-recompiled.sh

# 5. Configure + build (clang-cl + Ninja, from a VS env / x64 Native Tools prompt)
cmake -S . -B build-cmake -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
# -> build-cmake/BeetleRecomp.exe (+ SDL2.dll, dxcompiler.dll, dxil.dll copied beside it)
```
Or open the folder in **CLion** (Toolchain = Visual Studio, C/C++ compiler = `clang-cl`, generator = Ninja).
See [BUILDING.md](../BUILDING.md).

## Fixes / workarounds applied (and why)
1. **UI off by default** (`-DBEETLE_ENABLE_UI=OFF`) — RmlUi requires Freetype (on Windows: add a
   `lib/freetype-windows-binaries` submodule). Off until the UI layer is wired.
2. **`__PRFCHWINTRIN_H` for `rt64`** — clang 22 treats `_m_prefetch` as a builtin, but RT64's
   vendored SDL2 2.26.3 also defines it (`SDL_endian.h`).
3. **`scripts/fix-recompiled.sh`** — N64Recomp emits `0 = MEM_W(...)` for `lw $zero` (invalid C);
   rewrite to `(void)MEM_W(...)`. **Re-run after every regeneration** (`RecompiledFuncs/` is git-ignored).
4. **`src/main/os_unimpl_stubs.cpp`** — no-op stubs for 9 low-level libultra OS functions that
   N64Recomp lists in `ignored_funcs`, `librecomp` doesn't provide, and BAR calls anyway:
   `__osViSwapContext`, `__osTimerInterrupt`, `__osSiGetAccess/RelAccess/RawStartDma`,
   `osPiReadIo`, `__osPiRawReadIo`, `__osPopThread`, `__osEnqueueThread`. **Link-only — NOT correct at runtime.**
5. **N64Recomp `ffb39cd`** — `81213c1` offloads libc (`memcpy`/`sprintf`/…) to a runtime that doesn't
   implement them; `ffb39cd` recompiles libc into `RecompiledFuncs`.
6. **`src/main/main.cpp`** — the not-yet-generated RSP audio ucode (`aspMain`) reference is neutralized.

## Done this session (2026-06-28 night) — boot now reaches BAR's module loader
- **RT64 render context** — `src/main/rt64_render_context.cpp` (adapted from Zelda64Recompiled,
  MIT): an `RT64::Application`-backed `RendererContext`. D3D12 init confirmed.
- **SDL window** — `create_gfx`/`create_window`/`update_gfx` in `main.cpp` open a real window and
  return the Win32 `WindowHandle{ HWND, thread_id }`. `SDL_MAIN_HANDLED` so our `main()` is used.
- **ROM identity** — `rom_hash = 0x56cfec69d7951f9fULL` (XXH3-64 of the z64), `internal_name =
  "Beetle Adventure Rac"` (20 bytes at ROM 0x20). Verified vs librecomp `check_hash`/`select_rom`.
- **Overlay table registered** — `src/main/register_overlays.cpp` includes `recomp_overlays.inl`
  and calls `recomp::overlays::register_overlays` (`num_code_sections = ARRLEN(section_table)`,
  `total_num_sections = num_sections (413)`).
- **ROM auto-start (no UI)** — `main()` does `select_rom` then a *deferred* `start_game` on a
  detached thread, gated on `g_bar_renderer_ready` (set by the RT64 factory) + 150 ms.
- **VI null-mode crash fixed** — the VI thread only seeds a dummy `OSViMode` while
  `!is_game_started()`; starting the game before the renderer/VI thread is up made `update_vi()`
  deref a null mode. The deferred start above fixes this.
- **`scripts/fix-recompiled.sh`** now also rewrites 218 empty reloc `.type` fields in
  `recomp_overlays.inl` → `R_MIPS_NONE` (raw ELF reloc types > N64Recomp's `reloc_names`; inert
  for base-game execution — the section reloc table is only read by the mod system).
- **CMake** — added the two new TUs, `RecompiledFuncs/` to the include path, RT64's include set
  via `$<TARGET_PROPERTY:rt64,INCLUDE_DIRECTORIES>`, and `BEETLE_DEBUGINFO` (ON) for `/Z7`+`/DEBUG`
  symbols on the boot-path targets (RT64 stays Release).

## Still stubbed / not done
- Most `os_unimpl_stubs.cpp` functions stay no-ops, but **`__osSiRawStartDma` now has real behavior**
  (posts the SI completion + writes a controller-read response into PIF RAM — this is what unblocked input
  and the black screen). Others (`__osViSwapContext`, `__osTimerInterrupt`, PI/thread-queue) remain no-ops.
- **Input:** keyboard → N64 pad is wired (`bar_poll_keyboard`); real gamepad mapping + a rebind UI are TODO.
- **High-FPS / interpolation:** `enable_instant_present` is still a no-op and `get_display_framerate`
  returns 60 — RT64 frame interpolation is not yet enabled. Design + a partial implementation live on branch
  `feature/settings-menu-and-high-fps`; see [SETTINGS_MENU_AND_HIGH_FPS.md](SETTINGS_MENU_AND_HIGH_FPS.md).
- **Settings menu:** none yet (env-var flags only — `BAR_*`); the in-game menu is on the feature branch.

## How to build + run + debug (Windows, headless)
```bash
CMAKE="/c/Users/Bryan/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/cmake.exe"
"$CMAKE" -S . -B build-cmake          # clang-cl + Ninja are cached; clang-cl auto-detects MSVC
"$CMAKE" --build build-cmake -j
# Run (Release = /SUBSYSTEM:WINDOWS, so redirect to see logs):
cd build-cmake && ./BeetleRecomp.exe >run.log 2>&1   # ROM path: argv[1], else hardcoded Downloads path
# Symbolized backtrace at a crash:
"/c/Program Files/LLVM/bin/lldb.exe" --batch -o run -o bt -o quit -- ./BeetleRecomp.exe
```

## ✅ DONE: the uv module overlay CODE bridge
`src/main/overlay_bridge.cpp` owns the `uvDoModuleRelocs` symbol (the generated definition is
renamed to `uvDoModuleRelocs_orig` by `fix-recompiled.sh`). On each module load it reads the
4-char `nameTag` from `ModuleCommInfo` (a1+0x1C), maps it to the overlay id via a 133-entry
`nameTag -> overlay_id` table (overlays.us.txt order; tags from the decomp's
`tools/daisybox/src/bar_module_files.c`), then `unload_overlay_by_id` + `load_overlay_by_id(id,
ovlStartPtr)` and runs `uvDoModuleRelocs_orig`. Result: 57+ modules register, `func_map` resolves,
recompiled module code runs. (Also: VI null-mode race fixed via `g_bar_vi_ticked` gate. The RSP audio
task `aspMain` is now wired in `get_rsp_microcode` — no longer stubbed; and `hw_stubs.cpp`'s AI-length read
now returns the real queued length, not 0 — see WIP item 2.)

## ✅ RESOLVED: black screen (was a controller/SI deadlock, not gfx)
The "no `M_GFXTASK` submitted" symptom was downstream of a **controller (SI) `osRecvMesg` deadlock**:
BAR drives the controller through the low-level libultra path (`__osSiRawStartDma`), which our runtime
stubbed — so the game blocked forever waiting for an SI-complete event and never reached its render. **Fix**
(`src/main/os_unimpl_stubs.cpp`, commit `b92a2cc`): `__osSiRawStartDma_recomp` posts the SI completion
(`ultramodern::send_si_message()`) and writes a real controller-read response into PIF RAM. Also forced
`__osMaxControllers` to 4 (`fix-recompiled.sh` rule B) so a `READ_BUTTON` command is actually packed. The
game now renders at 60 fps and reads keyboard input.

## Visual verification — standard practice
**To see what's on screen, capture RT64's internal render — never a window-manager screenshot.** RT64's
present is read back GPU→PNG headlessly (no window focus needed), so it's the ground truth and works in
CI/background runs. Standard tools (all in **[HEADLESS_TESTING.md](HEADLESS_TESTING.md)**):
- `BAR_SHOTS="frame:path …"` — capture at scripted input frames (aligned with `BAR_AUTOPLAY`).
- `BAR_SHOT_BURST="fc:dir:count"` — record N consecutive presents to `dir/fNNNN.png` for **animations**
  the input timeline can't sample (e.g. the film-roll, where the game blocks in a render loop).
- `RT64_SHOT_TRIGGER`/`RT64_SHOT_OUT` — ad-hoc file-triggered single capture.

Combine with `BAR_SKIP_LAUNCHER=1` + `BAR_AUTOPLAY` to drive the game to any screen and capture it. Don't
assume where you are — capture and look (see memory `verify-on-the-actual-screen`).

## Common diagnostic env flags
`BAR_FPS` (fps counter) · `BAR_NO_AUDIO` (disable audio) · `BAR_AUDIO_DBG` (queue min/max + underrun + peak/
clip) · `BAR_AUDIO_BUFFER=<vi>` (audio buffer depth, default 2.5) · `BAR_AUDIO_XFORM` / `BAR_AUDIO_CAPTURE`
(PCM transform / capture) · `BAR_DEBUG_OVERLAYS` (log module loads) · `BAR_NO_INTRO_SKIP` (disable A/B/Start
intro skip) · `BAR_AUTOPLAY="frames:hexbtn …"` (scripted input: A=8000 B=4000 START=1000 UP=0800) ·
`BAR_FORCESTATE="frame:state …"` (force `gGameSettings->gameStateFlag`) · `BAR_PRESENT_MODE=console|skip|early`
(RT64 present mode; default **console** = present from the VI origin, so VI-origin menu transitions like the
film-roll animate cleanly with no pre-pan flash. `skip`=SkipBuffering, `early`=PresentEarly/min-latency) ·
`BAR_INSTANT_PRESENT` (legacy alias for `early`).

## Remaining roadmap (after the current polish items)

> The forward-looking checklist now lives in **[TODO.md](TODO.md)** (consolidated todolist,
> including the 7 priorities raised 2026-06-30). The summary below is kept for context.

- **Polish items 1/3/4** above (legal-screen skip, menu-transition flash, track-map fps).
- **High-FPS / RT64 interpolation** — enable it (branch `feature/settings-menu-and-high-fps`).
- **Settings menu** — surface the `BAR_*` flags (intro skip, audio buffer, high-FPS) in an in-game menu.
- **Real gamepad input** + a rebind UI (keyboard works today).
- Per-game F3DEX2 rendering quirks as they surface.

## Tooling note: N64Recomp builds on Windows now
`lib/N64Recomp` (already at `ffb39cd`) builds with clang-cl + CLion's cmake/ninja →
`lib/N64Recomp/build-win/N64Recomp.exe` (pass `-DCMAKE_RC_COMPILER` to a Win SDK `rc.exe`). WSL is
unusable for it (gcc 9.4, no cmake, sudo needs a password). Regen isn't needed for threading —
N64Recomp's built-in symbol lists already defer libultra OS funcs to ultramodern.

## Key references
- Git history: `f92cdb0` (fixup script), `890a87f` (build green), `cfec695` (overlays wired).
- `recomp.elf` comes from **bryankruman/BeetleDecomp** `make recomp` (`tools/genRecompLd.py`,
  `MODULE_VRAM_BASE = 0x80800000`).
- Upstream patterns to adapt: Zelda64Recompiled (`src/main`, `CMakeLists.txt`),
  N64ModernRuntime (`librecomp`, `ultramodern`).
