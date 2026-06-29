# Project Status & Resume Guide

_Last updated: 2026-06-28_

## TL;DR
- ✅ **Builds + links** → `build-cmake/BeetleRecomp.exe` (native Windows, clang-cl).
- ✅ **Boots deep into the game** (2026-06-28 night) — RT64 (D3D12, confirmed on an RTX 3080)
  initializes, the SDL window opens, the ROM is recognized, libultra inits, the **uv module
  overlay bridge** registers modules as they load (57+ modules, `func_map` resolves with zero
  "Failed to find function" errors), and recompiled module code executes.
- ✅ **Runs without crashing** — 78 modules register correctly (zero "Failed to find function"),
  audio + VI work, RT64 presents at 60 Hz. The earlier crashes were a **wrong nameTag→overlay_id
  table** (the `*_rom`/`*ld_rom` pairs were swapped); fixed from the authoritative
  `lib/bar-decomp/tools/convPartialModule.py`. (The "recomp.ld exports-layout" theory was WRONG —
  an artifact of comparing two swapped modules; recomp.ld is fine.)
- ✅ **Minimal controller wired** — reports port 1 connected, so the game passes its controller
  check (no longer drops into `uvShowNoController`). Real key/stick mapping still TODO.
- ⛔ **Black screen** — `func_80000450` (game main) is running its main loop but the per-frame render
  submits no `M_GFXTASK` (`send_dl` = 0), so the (real) framebuffer is never drawn. Not a
  crash/stub. See [Next step](#next-step-black-screen-game-main-runs-but-submits-no-gfx).

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

## Done this session (2026-06-29) — high-FPS interpolation + settings persistence
See [SETTINGS_MENU_AND_HIGH_FPS.md](SETTINGS_MENU_AND_HIGH_FPS.md) for the full design.
- **High-FPS (Phase 1) wired** in `src/main/rt64_render_context.cpp`:
  - `get_display_framerate()` now returns RT64's real monitor rate (`app->appWindow->getRefreshRate()`,
    fallback 60) so `RefreshRate::Display` tracks 144/165/etc. instead of being pinned to 60.
  - `enable_instant_present()` now sets `enhancementConfig.presentation.mode = PresentEarly` +
    `updateEnhancementConfig()` (was a no-op) so the game thread stops blocking on RDP completion.
  - These let RT64 *interpolate* display frames; **game speed is unchanged** — the VI/audio clock is
    `60 × speed_multiplier` with `speed_multiplier` a compile-time `1` the render path can't touch.
- **Settings persistence (host-owned)** — new `src/game/config.{hpp,cpp}` (`bar::config`):
  - Resolves a per-user dir (`%LOCALAPPDATA%\BeetleRecomp\`, or the exe dir if `portable.txt` exists;
    `~/.config/BeetleRecomp` on Linux) and points `register_config_path()` at it (was cwd).
  - Loads/saves `graphics.json` against ultramodern's `GraphicsConfig`; on first run writes defaults.
  - `main()` now calls `load_and_apply_graphics()` before `recomp::start()`, which is what actually
    turns interpolation **on**: the default `rr_option = Display` (every other field mirrors the prior
    built-in behavior). Edit `graphics.json` (`"rr_option": "Original"|"Display"|"Manual"`,
    `"rr_manual_value": 144`) to change it until the menu lands.
- CMake: added `src/game/config.cpp` and `src/` to the include path.

## Still stubbed / not done
- The 9 OS functions (`os_unimpl_stubs.cpp`) remain no-ops (threading / VI / SI / PI / timer).
- Audio (RSP `aspMain` ucode) and input (SDL→N64) are stubs.
- `get_resolution_scale` is minimal (fine for bring-up).
- **Settings menu UI**: not built yet (Tier 1/2 per the design doc). Config is currently file-only
  (`graphics.json`); the menu is the next deliverable. High-FPS artifact polish (extended-GBI
  transform tagging) for the HUD/particles is a later, per-game effort.

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
recompiled module code runs. (Also: VI null-mode race fixed via `g_bar_vi_ticked` gate; audio RSP
task no-op-stubbed in `get_rsp_microcode`; AI-length hardware read stubbed in `hw_stubs.cpp`.)

## Next step: black screen (game main runs but submits no gfx)
Narrowed via wrap-instrumentation (`fix-recompiled.sh`-style rename → native wrapper). Findings:
- `update_screen` runs at 60 Hz; VI origin moves dummy (`0x00700280`) → real fb (`0x00100280`).
- `send_dl` count = **0** — no `M_GFXTASK` ever submitted → the presented fb is never drawn → black.
- **`uvShowNoController` is NOT entered** — the game passed its controller check
  (`game_init.c:117 gUvContExports->unk8(0)`), thanks to the wired minimal controller.
- **`func_80000450` (the game main, `game_init.c:23`) entered and never returned** — so it's running
  its main loop `while (gUvContExports->unk4() != 0) { gGameExports->unk4(); … }` (game_init.c:133)
  but the per-frame `gGameExports->unk4()` produces no display list. Game is alive (audio + VI run).

So the stall is inside the **game module's per-frame render** (`gGameExports->unk4`) or the loop
condition (`gUvContExports->unk4`) — not a crash/stub/missing-func, and not the no-controller path.

**Blocker:** no headless debugger works here — lldb *attach* hangs after the attach breakpoint;
`comsvcs.dll MiniDump full` produces a dump lldb rejects ("data is invalid") and won't make a
standard (non-full) dump; no cdb/procdump installed. And `gGameExports->unk4`/`gUvContExports->unk4`
are reached via exports structs filled in **undecompiled asm entrypoints**
(`__entrypoint_func_{game,uvcont}_rom_400000.s`), so finding their recompiled `func_*` names to wrap
is slow.

**To do (pick one):**
1. **Get one thread stack** — attach **Visual Studio** to `BeetleRecomp.exe`, Break All, inspect the
   `Game …`/App thread (the one in `func_80000450`/`uvSetGameState`/an `osRecvMesg`). That pinpoints
   the stall in minutes. (Native Windows debugger; no PDB hang.)
2. **Keep instrumenting** — find the recompiled `func_game_*`/`func_uvcont_rom_*` that the exports
   `unk4` fields point to (read the recompiled module entrypoints in `RecompiledFuncs/`), wrap them
   to see whether the loop iterates and whether the frame fn runs/bails.

## Remaining roadmap (after the black screen)
- **Input** (SDL → N64 controller) — then the title screen is actually playable.
- **RSP audio** (`RSPRecomp` → real `aspMain`), wired into `get_rsp_microcode`.
- Replace the 9 OS-func stubs with correct behavior if any are reached.
- Per-game F3DEX2 rendering quirks once frames are being submitted.

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
