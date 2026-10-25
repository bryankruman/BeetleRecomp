# Building BeetleRecomp

The host application uses CMake, Ninja and a C++20 compiler: clang-cl with the Windows
SDK on Windows, Clang on Linux, or Apple Clang on macOS. RT64 uses D3D12 on Windows, Vulkan on Linux, and native Metal on macOS.
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
python3 scripts/generate-original-bindings.py resolve \
  --elf elf/recomp.elf --policy config/original-functions.us.json \
  --policy-sha256 a785fc8a8552d22e6cd39403535b247af340b0d6287d60082d7483caefb10a11 \
  --base-config BeetleRecomp.toml --output-toml elf/BeetleRecomp.original-bound.toml
./N64Recomp elf/BeetleRecomp.original-bound.toml
bash scripts/fix-recompiled.sh
python3 tools/gen_reference_syms.py elf/recomp.elf syms/beetleadventurerac.us.syms.toml syms/beetleadventurerac.us.datasyms.toml
PATCH_CC=clang-18 PATCH_LD=ld.lld-18 bash scripts/build-patches.sh
./N64Recomp patches.toml
```

The original-function policy is metadata only. Its 29 logical requirements remain
in `BeetleRecomp.toml`; the resolver verifies real positive ELF functions, extents
and exact instructions, then emits expected bytes only under ignored
`elf/*.original-bound.toml`. The patched recompiler validates every row before
renaming the same existing function index. It rejects missing/ambiguous providers,
stubs and intersecting mutation policies. Do not substitute a stub or remove a
requirement when resolution fails.

The resolver creates its output exclusively. Before intentionally regenerating,
retire only the old ignored `elf/BeetleRecomp.original-bound.toml`, then rerun the
resolver against the current ELF. Never commit or upload the private binding file.
Dependency setup recognizes a pristine pinned N64Recomp checkout, the previous
reviewed original-function patch, or the complete current patch. The previous
patch migrates through a separate checked incremental patch; setup does not
reset or reverse-write local source. An incompatible checkout fails during
the shared preflight before any dependency patch is applied.

Run `python3 tests/test_original_bindings.py` for lightweight synthetic parser,
identity and Git privacy controls; those controls contain no game data.

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


## Apple Silicon / M1 MacBook Air

The macOS source path now creates `BeetleRecomp.app`, compiles the frontend's Metal
shaders with the pinned native ARM64 compiler, and stages fonts/styles/icons under
`Contents/Resources/assets`, where the Cocoa frontend actually reads them. This
is source preparation, not a passed macOS release qualification. A native Mac
build and the runtime checklist in `docs/MACOS_REVIEW.md` remain required.

Use a native ARM64 terminal and an Xcode installation with working `metal` and
`metallib` tools (`xcrun --sdk macosx --find metal`). Install CMake, Ninja, Python 3,
SDL2 development files and Freetype using your chosen package manager before
building. Select Xcode in the normal developer-tools configuration if necessary.
The wrapper installs nothing. Use the existing verified generated game/patch C;
Apple's Clang need not provide the separate MIPS backend needed to regenerate it.

```bash
bash scripts/setup.sh
# Generate or privately transfer verified RecompiledFuncs/ and RecompiledPatches/
# using the original-binding and fixup procedure above, then:
BAR_JOBS=4 bash scripts/build-macos.sh
open build-macos/BeetleRecomp.app
```

The wrapper refuses Rosetta shells and missing Metal tools/generated patches.
`BAR_BUILD_DIR` and `BAR_JOBS` override its build directory and parallelism.
For this campaign, do not run a full build on Skytech; the approved Linux worker
cannot validate Cocoa, Apple Clang, Metal, or the M1 GPU without an Apple SDK/host.

The development `.app` links local SDL2/Freetype and their dependencies. It is **not
self-contained** for copying to another Mac until those libraries have been bundled,
relocated and signed. Do not publish a development app as a portable release.
Build on the target Mac for personal testing, or finish native package qualification
before distributing it. A release must include dependency/font licenses and matching
source while excluding all ROMs, ELFs, saves and user configuration. No notarized or
signed macOS release has been produced by this Windows review.

After a native build, stage a self-contained app for personal testing with:

```bash
python3 scripts/package-macos.py --version 0.3.0-mac-test \
  --sdl-license /path/to/SDL2-COPYING.txt \
  --dependency-licenses /path/to/actual-macos-dependency-notices
```

This opt-in packager copies only the executable, Info.plist and UI assets into a
new `dist/` folder, collects vendored/SDL licenses plus the supplied notices for
the actual external SDL/Freetype/transitive-library builds, uses CMake BundleUtilities to relocate the
required dynamic libraries, rejects unresolved/Homebrew dependencies and non-ARM64
libraries, records the highest minimum macOS version required by any bundled ARM64
image in Info.plist, then signs nested code and the app ad-hoc. It verifies the signature
before creating the zip. Existing outputs are never overwritten. This is not
Developer ID signing or notarization; macOS may still require the normal explicit
user approval for downloaded unsigned software. Do not disable system security.
The provided notice directory must cover the exact external library builds; the
packager checks text/file boundaries, not legal completeness. The script has not
been executed on macOS in this review. Distribution still needs
its native runtime checks and exact corresponding source obligations satisfied.

Run `python3 tests/test_macos_build.py` for host-tool selection, missing-tool rejection
and bundle resource contract checks. These do not compile or execute Apple code.

For a native check of Retina coordinate conversion after building, run:

```bash
c++ -std=c++20 $(sdl2-config --cflags) \
  -Ilib/RecompFrontend/recompui/src tests/retina_coordinates_regression.cpp \
  -o build-macos/retina_coordinates_regression
./build-macos/retina_coordinates_regression
```

The minimum supported macOS version follows the actual linked SDK/dependency
artifacts. The packager derives it from every bundled ARM64 Mach-O image; this
source review does not promise support for a particular older macOS release.
