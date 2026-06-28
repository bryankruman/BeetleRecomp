# Building BeetleRecomp

> Early scaffold. The steps below describe the intended pipeline; expect to adapt the CMake
> link wiring from
> [Zelda64Recompiled's BUILDING.md](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/BUILDING.md)
> until the runtime is fully wired in `src/main`.

## Prerequisites

**Common:** CMake ≥ 3.20, Ninja, a C++20 compiler, and an LLVM toolchain (`clang`, `ld.lld`) —
the patches build compiles C to MIPS with `clang -target mips`.

### Windows (recommended target — RT64 uses Direct3D 12)
- Visual Studio 2022 with: *Desktop development with C++*, *C++ Clang Compiler for Windows*,
  *C++ CMake tools for Windows*.
- [LLVM](https://github.com/llvm/llvm-project/releases) on `PATH` (for the MIPS patch build).
- `make` for `patches/Makefile` (e.g. `choco install make`).
- Git, with the WSL decomp reachable for `scripts/fetch-elf.ps1`.

### Linux / WSL (RT64 uses Vulkan)
```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang make
```

## Step-by-step

### 1. Dependencies + recompiler
```bash
scripts/setup.sh            # Git Bash / WSL
# or
pwsh scripts/setup.ps1      # Windows PowerShell
```
Adds the submodules under `lib/` (including the decomp at `lib/bar-decomp`), then builds
`N64Recomp` and `RSPRecomp` and copies the executables to the repo root. If you cloned without
`--recursive`, this also runs `git submodule update --init --recursive`.

### 2. Provide the game ELF
The recompiler needs the decomp's symbol-rich ELF. Build it in WSL, then copy it in:
```bash
# In WSL:
cd ~/projects/bar-decomp && source .venv/bin/activate && make -j6
# Back in the recomp repo:
scripts/fetch-elf.sh        # streams build/beetleadventurerac.us.elf -> elf/
```
> See `BeetleRecomp.toml`. Long-term, point `elf_path` at a **per-module `recomp.elf`**
> (see README → Roadmap), not the 170 MB matching ELF.

### 3. Recompile to C
```bash
./N64Recomp BeetleRecomp.toml      # -> RecompiledFuncs/*.c
# (later, once patches exist:)
# make -C patches                  # -> patches/patches.elf
# ./N64Recomp patches.toml         # -> RecompiledPatches/*.c
```

### 4. Build the port
```bash
cmake -S . -B build-cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
```
Run the resulting `BeetleRecomp` executable from the repo root (or with `assets/` beside it).

## Notes
- The recompiler runs **manually, before** the CMake build — it generates the C that CMake
  compiles. Re-run it whenever the ELF or a `*.toml` changes.
- The **decomp** builds only on a Linux/WSL IDO/MIPS toolchain; it does **not** build natively
  on Windows. Only the **recomp** builds natively on Windows.
- The decomp is vendored at `lib/bar-decomp` (submodule) for headers/symbols. To adopt decomp
  improvements: `git submodule update --remote lib/bar-decomp`, then rebuild the ELF at that
  commit in WSL and re-run the recompiler. Do **not** build the decomp from the NTFS submodule.
- Keep this repo LF-normalized (`.gitattributes` enforces it) even on Windows. Unlike the
  decomp (which must live on the WSL ext4 filesystem), this repo is fine on NTFS / native Git.
