# Project Status & Resume Guide

_Last updated: 2026-06-28_

## TL;DR
- ✅ **Builds + links** → `build-cmake/BeetleRecomp.exe` (native Windows, clang-cl).
- ❌ **Does not run the game yet** — the runtime host is stubbed (RT64 not wired, audio/input stubs).
- Next phase: **runtime bring-up** (see [Roadmap](#roadmap-to-a-running-title-screen)), starting with the RT64 render context.

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

## What's stubbed (this is why it doesn't run)
- `create_render_context` returns `nullptr` → **RT64 not initialized** (no graphics).
- `create_gfx` / `create_window` / audio / input callbacks are stubs.
- the 9 OS functions are no-ops (threading / VI / SI / PI / timer).
- `GameEntry.rom_hash` (needs the **XXH3-64 of the z64 ROM**, not the SHA-1) and `internal_name`
  (the 20-byte name at ROM offset `0x20`) are placeholders.

## Roadmap to a running title screen
1. **RT64 render context** — port Zelda64Recompiled's `src/main/rt64_render_context.cpp`
   (a real `RT64::Application`) to replace the `create_render_context` stub. Biggest single item.
2. **ROM load + window** — real `rom_hash`/`internal_name`; SDL `create_gfx`/`create_window`.
3. **Module/overlay runtime registration** — wire BAR's relocatable modules' load/relocation.
4. **Input** (SDL → N64 controller), then **RSP audio** (`RSPRecomp` → real `aspMain`).
5. Replace the 9 OS-func stubs with correct behavior (threading especially) once it's running.

## Key references
- Git history: `f92cdb0` (fixup script), `890a87f` (build green), `cfec695` (overlays wired).
- `recomp.elf` comes from **bryankruman/BeetleDecomp** `make recomp` (`tools/genRecompLd.py`,
  `MODULE_VRAM_BASE = 0x80800000`).
- Upstream patterns to adapt: Zelda64Recompiled (`src/main`, `CMakeLists.txt`),
  N64ModernRuntime (`librecomp`, `ultramodern`).
