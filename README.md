# BeetleRecomp

A work-in-progress **native PC port of _Beetle Adventure Racing!_ (N64, USA)** built by
**static recompilation** with the [N64Recomp][N64Recomp] toolchain — the same approach
behind [Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp).

> **Status (2026-06-28): boots deep into the game.** RT64 (D3D12) initializes, the window opens,
> the ROM is recognized, libultra inits, and the **uv module overlay bridge** registers BAR's
> relocatable modules as they load (57+) so recompiled module code runs. It is **not playable
> yet** — it stops on module data-section relocation (the deep part of the ~130-module challenge),
> and audio/input are still stubs. See **[docs/STATUS.md](docs/STATUS.md)** to resume.

> **No game data is included.** You must supply your own legally-dumped USA ROM
> (SHA-1 `e5ab4d226c08d22f68a2edcc48870203e67454b8`). No ROM, assets, or other copyrighted
> material may ever be committed to this repo.

## What this is

Static recompilation translates the N64's MIPS machine code into C automatically, then links
it against a modern runtime (CPU via [librecomp], graphics via [RT64], OS/audio/input via
[ultramodern]). It is **not** a manual rewrite and does **not** require a finished
decompilation.

This port is a sibling to the **[BeetleDecomp](https://github.com/bryankruman/BeetleDecomp)**
decompilation. The decomp is *not* a prerequisite for the recomp, but it is the source of the
**symbol-rich ELF** and the **module/relocation metadata** the recompiler consumes — which is
what makes this port tractable.

## Relationship to the decomp

This repo consumes the **[BeetleDecomp](https://github.com/bryankruman/BeetleDecomp)** project,
vendored as a submodule at `lib/bar-decomp`. Two things are important to understand:

**The recomp does *not* compile the decomp's C.** Static recompilation translates the original
ROM's MIPS *machine code* into C automatically — the decomp's hand-written C is never built into
the port. What the recomp uses from the decomp is:

| From the decomp | What for | How it gets here |
|---|---|---|
| The **ELF** (machine code + symbols) that rebuilds the ROM byte-for-byte | Input to N64Recomp | built in WSL → `scripts/fetch-elf.*` (artifact, never committed) |
| **Headers + symbol tables** (`include/`, `linker_scripts/us/symbol_addrs.txt`) | Readable names + typed interfaces for generated code and patches | `lib/bar-decomp` submodule (source) |
| **Segmentation / module + reloc layout** (`config/us/modules.yaml`, future `recomp.ld`) | Overlay handling | `lib/bar-decomp` submodule (source) |

**Decomp matching progress does not change the game.** Because the decomp is *byte-matching*, a
function written in C produces the exact same machine code as its raw-asm version — so recompiling
it yields identical output. Matching progress is therefore not a prerequisite for the port. What *does* flow downstream from
decomp improvements is: **better symbol names**, **struct/types** (readability of generated code and
patches), and — most importantly — **segmentation and reloc layout** (the per-module `recomp.ld`,
see [Roadmap](#roadmap)).

**Pulling decomp updates is deliberate, not silent.** The submodule is pinned to a commit. To adopt
decomp improvements: `git submodule update --remote lib/bar-decomp`, then rebuild the ELF *at that
same commit* in WSL and re-run the recompiler. (Keep your WSL decomp checkout and this submodule on
the same commit so the ELF and headers agree.)

> **Build-locality caveat:** do **not** run the decomp's `make` on this Windows-checked-out
> submodule — its IDO/MIPS build needs the WSL ext4 filesystem (NTFS hits exec-bit/perf problems).
> The submodule here is for *source reference* (headers/symbols); build the ELF in your WSL decomp
> checkout and fetch it in.

### Where modifications go (the modifier layer)

**Never edit the decomp to change the game.** Keep it a faithful, byte-matching mirror of the
original. Modifications live in two recomp-owned layers:

- **`patches/`** — C compiled to MIPS that **overrides** game functions by name (`RECOMP_PATCH`) or
  **hooks** their entry/return (`RECOMP_HOOK`). This is the game-logic modifier layer: bug fixes,
  physics tweaks, widescreen, new features. Patches link *before* the recompiled output, so they win.
- **`src/` runtime** — the native C++ host: rendering (RT64), input, audio, saves, config, resolution,
  and any new capabilities. This is the "interpreter / host" layer.
- For distributable mods, the recomp ecosystem also has a **mod system** (RecompModTool / `.nrm`).

The decomp stays a pure reconstruction of the original; all added or changed code lives here.

## Verified findings (2026-06-28)

| Question | Result |
|---|---|
| **Graphics microcode** | Stock **`gspF3DEX2_fifo`** (decomp `gfx_ucode: f3dex2`) — *not* a custom Paradigm ucode. **RT64 supports F3DEX2 directly.** |
| **Audio** | Standard libultra (`alAudioFrame`/`alSeqFileNew`/ALSndPlayer) wrapped in Paradigm's "UV" middleware. Recompilable. |
| **Saves** | Controller Pak only (`osPfs*`); no EEPROM/SRAM/Flash. The port must emulate a Controller Pak. |
| **Memory** | 4 MB; **no Expansion Pak**, no TLB-mapped code (flat KSEG0). Favorable. |
| **Entry point** | `0x80000400` (ROM header offset 0x8 == splat `entry` segment vram). |
| **Candidate quality** | **Good** — no custom-ucode blocker. |
| **Biggest challenge** | The **~130 relocatable code modules** (`ai`, `battle`, `race`, …, `uv*_rom`). Supported by N64Recomp, but this is where the work lives. |

The toolchain license analysis and research sources are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## How it fits together

```
your ROM ─┐
          ├─► BeetleDecomp (WSL) ──► symbol-rich ELF ──► N64Recomp ──► RecompiledFuncs/*.c ─┐
recomp.ld ┘   (per-module layout)                       (BeetleRecomp.toml)                 ├─► CMake ─► BeetleRecomp(.exe)
                                                                                            │
 patches/*.c ─► clang -target mips ─► patches.elf ─► N64Recomp (patches.toml) ─► RecompiledPatches/*.c ┘
                                                            │
            runtime: librecomp + ultramodern + RT64 ───────┘
```

## Repository layout

```
BeetleRecomp/
├── BeetleRecomp.toml      # main N64Recomp config (entrypoint 0x80000400, ELF, overlays)
├── patches.toml           # config for the C patches (single-file override mode)
├── overlays.us.txt        # relocatable module/overlay section list (see Roadmap)
├── CMakeLists.txt         # build skeleton (adapt link wiring from Zelda64Recomp)
├── COPYING                # AGPL-3.0 (inherited from the decomp)
├── THIRD_PARTY_NOTICES.md # dependency licenses + research sources
├── BUILDING.md            # full build instructions (Windows + Linux/WSL)
├── elf/                   # the decomp ELF lands here (git-ignored)
├── syms/                  # symbol TOMLs for patches (git-ignored)
├── lib/                   # submodules: N64ModernRuntime, rt64, RmlUi, lunasvg, N64Recomp
├── patches/               # C compiled to MIPS that overrides game functions
├── src/{main,game,ui}/    # the native port runtime (C++)
├── include/               # port headers
├── assets/  icons/        # bundled app assets
└── scripts/               # setup / fetch-elf / gen-overlays helpers
```

## Quickstart

> Full details and prerequisites: **[BUILDING.md](BUILDING.md)**.

```bash
# 1. Fetch dependencies and build the recompiler (N64Recomp + RSPRecomp).
scripts/setup.sh                  # or scripts/setup.ps1 on Windows PowerShell

# 2. Build the decomp ELF (in WSL), then copy it in.
#    (WSL) cd ~/projects/bar-decomp && source .venv/bin/activate && make -j6
scripts/fetch-elf.sh              # or scripts/fetch-elf.ps1

# 3. Recompile the game to C.
./N64Recomp BeetleRecomp.toml     # emits RecompiledFuncs/*.c

# 4. Configure + build the port.
cmake -S . -B build-cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
```

## Roadmap

The structure is in place; these are the real milestones, in order:

1. **Per-module recomp ELF (the crux).** The decomp's *matching* ELF
   (`beetleadventurerac.us.elf`) is 170 MB with 2,492 sections — splat splits each module into
   many per-file sub-sections (`.UVAN_0`, `.UVBT_0`, …), which is **not** the
   one-section-per-module grouping N64Recomp needs for relocatable overlays. The decomp already
   has a stubbed **`make recomp` → `build/recomp.elf`** target, but its
   `linker_scripts/us/recomp.ld` does not exist yet. **Writing that linker script (one
   relocatable section per module, with the module reloc tables) is the first task**, and it is
   shared work with BeetleDecomp.
2. **Overlay wiring.** Populate `overlays.us.txt` from the recomp ELF
   (`scripts/gen-overlays.sh`) and get N64Recomp to emit clean `LOOKUP_FUNC` / `RELOC_*` for
   every module.
3. **First boot.** Wire `src/main` to librecomp/ultramodern/RT64 (adapt from
   Zelda64Recompiled), load the ROM, reach the title screen.
4. **Graphics.** Confirm BAR's exact F3DEX2 sub-revision renders cleanly in RT64; fix per-game
   quirks (reflections/fog/menus that GLideN64 historically needed tweaks for).
5. **Input, audio, and Controller Pak saves.**
6. **Patches.** Override hardware-poking functions via `patches/` as needed.

## License

**AGPL-3.0**, inherited from the BeetleDecomp data this port derives from. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the dependency-license analysis and why
AGPL is the correct umbrella. Vendored dependencies in `lib/` retain their own licenses.
**Distribute no game assets.**

## Credits

- **SynaMax / synamaxmusic** and **LLONSIT** — original
  [bar-decomp](https://github.com/synamaxmusic/bar-decomp) / RE work this builds on.
- **Wiseguy** and contributors — [N64Recomp], [N64ModernRuntime], Zelda 64: Recompiled.
- **RT64 contributors** — the renderer.

[N64Recomp]: https://github.com/N64Recomp/N64Recomp
[N64ModernRuntime]: https://github.com/N64Recomp/N64ModernRuntime
[librecomp]: https://github.com/N64Recomp/N64ModernRuntime/tree/main/librecomp
[ultramodern]: https://github.com/N64Recomp/N64ModernRuntime/tree/main/ultramodern
[RT64]: https://github.com/rt64/rt64
