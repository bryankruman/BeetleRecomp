# Building BeetleRecomp

The host application uses CMake, Ninja and a C++20 compiler: clang-cl with the Windows
SDK on Windows, or Clang on Linux. RT64 uses D3D12 on Windows and Vulkan on Linux.
The MIPS viewport patches use a separate MIPS-capable **LLVM 18** Clang/LLD toolchain.
Those patches are required for the integrated release's widescreen behavior.

The October 6 fork integration passed complete Linux and Windows builds and initial
game checks, including Controller Pak initialization, cached saves, widescreen driving
and pause. See [docs/FORK_REVIEW.md](docs/FORK_REVIEW.md) for evidence and remaining
gameplay/release gates. macOS support is unverified.

## Dependencies and setup

Windows needs Visual Studio 2022 Build Tools with the C++ workload and Windows SDK,
clang-cl, CMake, Ninja and Python. Use an x64 Developer Command Prompt. SDL2 and the
frontend libraries are acquired by CMake or provided in the source tree.

Linux needs Clang/LLD, CMake, Ninja, Python, SDL2, GTK3, Freetype and Vulkan development
packages. Ubuntu 24.04 provides `clang-18` and `lld-18` for MIPS patches. Do not assume
every LLVM binary includes the MIPS backend: check `llc --version`.

```bash
bash scripts/setup.sh
```

Windows uses `scripts/setup.ps1`. Both initialize committed submodule pins, preflight
and apply `scripts/dependency-patches/`, then build N64Recomp/RSPRecomp. The patch script
never resets local edits. N64Recomp, N64ModernRuntime and RT64 remain Git submodules; RecompFrontend
is vendored with its nested notices/licenses. `BEETLE_BUILD_JOBS` defaults to 4.

## Windows shader libraries and cross-builds

RT64 links three embedded DXIL libraries again at runtime. The pinned DXC dependency
contains different compiler releases for Linux and Windows; a shared Git revision does
not make those binaries interchangeable. Keep the bundled Windows `dxc.exe`,
`dxcompiler.dll` and `dxil.dll` together.

Native Windows builds export the three libraries automatically into the build directory's
`windows-dxil-bank`, then validate/import that bank. Only three small shader compilations
run on Windows; the C/C++ build may still run on the isolated server. For a Linux-hosted
Windows build, first export a bank on Windows from the exact same shader/header sources:

```powershell
python scripts/windows-shader-bank.py export --source lib/rt64/src --bank C:/build/beetle-windows-dxil-bank
```

Copy that four-file bank into the server's approved build workspace. Configure the
existing Windows cross-toolchain with `-DBEETLE_WINDOWS_DXIL_BANK=/path/to/bank` and
`-DBEETLE_HOST_FILE_TO_C=/path/to/existing/native/file_to_c`. This does not install a
cross-toolchain or fetch dependencies. The existing native `file_to_c` must be built
for the Linux host; the compiler/SDK toolchain remains an explicit prerequisite.
Do not edit DXC assignments in the generated source snapshot. The tracked RT64 addon
selects Linux DXC for standalone shaders/SPIR-V and imports the three runtime-linked
libraries from the authenticated native bank.

The bank records every shader/shared file, transitive quoted header, exact compile
arguments, native compiler/DLL hashes and output hashes. Changing any source/header,
compiler/runtime binary or library invalidates it. Regenerate and transfer the bank
rather than disabling validation. The build and release packager verify each native
library appears exactly once in the executable and the packaged DLL pair matches.
No ROM, ELF, save or generated game code belongs in a shader bank.

## Generate private inputs and code

Supply your own USA ROM (`e5ab4d226c08d22f68a2edcc48870203e67454b8`) to the decomp build.
Verify its ROM output, then build the decomp's `recomp` target. Use that exact ELF for
main code and patch reference symbols. Keep ROMs, ELFs and generated/extracted files
out of public Git.

```bash
DECOMP_DIR=/path/to/bar-decomp bash scripts/fetch-elf.sh
./N64Recomp BeetleRecomp.toml
bash scripts/fix-recompiled.sh
python3 tools/gen_reference_syms.py elf/recomp.elf syms/beetleadventurerac.us.syms.toml syms/beetleadventurerac.us.datasyms.toml
PATCH_CC=clang-18 PATCH_LD=ld.lld-18 bash scripts/build-patches.sh
./N64Recomp patches.toml
```

From Windows Git Bash the patch wrapper enters `WSL_DISTRO` (default `Ubuntu-24.04`).
On another Linux host use native MIPS-capable LLVM 18. Do not disable fixup verification
for a release: an address anchor can silently miss after the decomp changes. Re-run both
N64Recomp steps/checks whenever the ELF, TOMLs or patches change. The generated audio
RSP source is `rsp/aspMain.cpp`; regenerate with the project's RSP configuration when
its inputs change.

## Build the host

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBEETLE_ENABLE_FRONTEND=ON -DBEETLE_REQUIRE_PATCHES=ON
cmake --build build --parallel 4
```

Windows uses `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` instead. Linux
may need versioned Clang names. `BEETLE_REQUIRE_PATCHES=ON` fails configuration when
generated patches are missing; private release CI uses it. OFF is for diagnostics and
does not produce a verified widescreen release.

Run `python tests/test_dependency_patches.py` for pristine dependency-patch checks.
On Windows, `powershell -File scripts/test-runtime-input.ps1 -BuildDir build` checks
isolated save fixtures and SDL virtual pads. `scripts/test-si-pak.ps1` checks the actual
SI handler's four-port errors and DMA bounds in isolated memory. These supplement the full-build/game
checklist in the fork review. Keep the repository LF-normalized.

`scripts/test-mod-dependencies.ps1` checks manifest control dependencies and numeric
schema bounds using the actual runtime parser/configuration code, without real mod or
configuration file access.

Heavy builds/searches for this workstation belong on the isolated pve worker; NetworkOps
requires approval for fleet changes; this campaign has a scoped worker-build authorization. Package with `scripts/package-release.ps1`,
including corresponding source and license notices. Never package user ROMs, saves,
configurations or private credentials.
