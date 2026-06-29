# Project Status & Resume Guide

_Last updated: 2026-06-28_

## TL;DR
- ✅ **Builds + links** → `build-cmake/BeetleRecomp.exe` (native Windows, clang-cl).
- ✅ **Launches + boots into game code** (2026-06-28 night) — RT64 (D3D12) initializes, the SDL
  window opens, the ROM is recognized, and the recompiled boot runs libultra init → the `App`
  thread → `func_80000450` (main) → `uvSetGameState` → BAR's module loader.
- ⛔ **Blocked on the uv module overlay bridge** — `uvLoadModuleCode` copies a relocatable module
  into a heap buffer and jumps to its entry; librecomp's `func_map` has no entry there, so
  `get_function(0x80042300)` aborts. This is the documented "~130 relocatable modules" challenge.
- Next: **bridge BAR's module loader → librecomp overlay registration** (see [Next step](#next-step-the-uv-module-overlay-bridge)).

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
- The 9 OS functions (`os_unimpl_stubs.cpp`) remain no-ops (threading / VI / SI / PI / timer).
- Audio (RSP `aspMain` ucode) and input (SDL→N64) are stubs.
- `enable_instant_present` is a no-op; `get_display_framerate` returns 60; `get_resolution_scale`
  is minimal (these are fine for bring-up).

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

## Next step: the uv module overlay bridge
The exact failure (symbolized backtrace):
```
get_function(0x80042300)  -> not in func_map -> abort        (overlays.cpp:367)
  uvLoadModuleCode  (decomp src/module.c:115)
  uvAllocFile / uvLoadFile / _uvAllocModule / uvLoadModule    (BAR's "uv" module system)
  uvSetGameState -> func_80000450 (main) -> Thread_App
```
`uvLoadModuleCode` (decomp `src/module.c`) does:
```c
headeredStartPtr = _uvMemAllocAlign16(exportsSize + blockSize + bssSize);  // heap buffer
ovlStartPtr      = headeredStartPtr + exportsSize;
_uvMediaCopy(ovlStartPtr, codeBlock, blockSize);   // copy module CODE into RAM
uvDoModuleRelocs(ovlStartPtr, &info);              // relocate the raw bytes
entryPointFunction = ovlStartPtr + info.entryPointOffset;
entryPointFunction(headeredStartPtr);              // <- the LOOKUP_FUNC that fails
```
So BAR loads a relocatable module into a heap buffer and jumps in. The recompiled module
functions live in `func_map` keyed by their recomp-ELF VRAM (`0x80800000+`), not the heap
address — nothing registers them at `ovlStartPtr`.

**The bridge:** when a module is loaded, register its recompiled section at `ovlStartPtr` via
`recomp::overlays::load_overlay_by_id(overlay_id, ovlStartPtr)` (or `load_overlays(rom, ram,
size)`). The work is the *mapping*: BAR module (`tag`/`fileId` in `uvGetModuleFileId`, or the
CODE block's ROM offset) → recomp overlay index / section (`overlay_sections_by_index`,
`get_vrom_to_section_map`). Mechanism options, cheapest first:
1. A `RECOMP_HOOK`/`RECOMP_PATCH` on `uvLoadModuleCode` (return) or `uvDoModuleRelocs` that calls
   the overlay-registration API with the load address — needs the patches MIPS toolchain
   (`patches.toml`, `clang -target mips`, the commented `PatchesLib` in CMake).
2. A native override of `uvLoadModuleCode` registered through librecomp.
   Confirm how the module's ROM/section is identified — compare `info`/`fileId` against the recomp
   ELF section `rom_addr`s in `recomp_overlays.inl`.

## Remaining roadmap (after the bridge)
- **Input** (SDL → N64 controller) — then the title screen is actually playable.
- **RSP audio** (`RSPRecomp` → real `aspMain`), wired into `get_rsp_microcode`.
- Replace the 9 OS-func stubs with correct behavior (threading especially).
- Per-game F3DEX2 rendering quirks once frames are being submitted.

## Key references
- Git history: `f92cdb0` (fixup script), `890a87f` (build green), `cfec695` (overlays wired).
- `recomp.elf` comes from **bryankruman/BeetleDecomp** `make recomp` (`tools/genRecompLd.py`,
  `MODULE_VRAM_BASE = 0x80800000`).
- Upstream patterns to adapt: Zelda64Recompiled (`src/main`, `CMakeLists.txt`),
  N64ModernRuntime (`librecomp`, `ultramodern`).
