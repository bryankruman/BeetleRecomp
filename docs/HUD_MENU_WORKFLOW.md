# HUD & Menu System — Writing Workflow

Companion to [`HUD_MENU_SYSTEM_MAP.md`](HUD_MENU_SYSTEM_MAP.md). That doc is the *map* (what every
HUD/menu module is and how the letterbox/pillars work). This doc is the *plan for turning it into
readable, documented, byte-matching C that is easy to modify* — plus the exact loop and verification
cadence to do it safely.

## The three phases

1. **Understand & map** *(done)* — the `bar-hud-menu-map` workflow inventoried all ~24 overlay
   modules + the uv framework and traced the letterbox/pillar mechanism end-to-end. Output = the map doc.
2. **Write** *(this doc)* — two tracks below.
3. **Verify** — per-function `./diff.py`, per-module hash, full-ROM SHA-1 gate (see *Verification*).

## Two writing tracks

### Track A — Readability / scribe (matched functions → name / type / comment)

The ~50% of HUD/menu functions that are **already byte-matched** are mostly `func_XXXXXXXX` /
`D_XXXXXXXX` with `char pad[..]; void (*unkNN)()` export shims. Making them legible is what turns
"read-only archaeology" into "easy to modify." **Renaming symbols and adding comments never changes
codegen → always matching-safe.** Struct/type changes *can* change codegen, so they must be
build-verified (see *Toolchain*).

Ordered by leverage — framework/exports first (unlocks every consumer), then the letterbox choke
points, then the most-touched screens:

| # | Target (file) | What to do | Why (leverage) |
|---|---|---|---|
| 1 | `uvgfxmgr_rom.c` (`func_..._00401C5C`, `..._00401BD4`, `uvGfxClipRect`) | Name the `UvGfxViewport` fields (`x0/x1/y0/y1`), type the `UvGfxMgr_Exports` slots, comment the clear→scissor pipeline | This IS the letterbox/scissor emitter; every module clips through it |
| 2 | `uvchannel_rom.c` (`func_..._00401278`, `..._00400288` case 5) | Type the channel struct `unk214/216/218/21A → x0/x1/y0/y1`; comment the cinematic-bar mechanism at the choke point | The single edit point for TOP/BOTTOM bars — make it self-documenting |
| 3 | `uvsprt_rom.c` (`uvDrawBitmap`, `uvSpriteDrawInit`, `sScissor*`) | Name the 2D sprite scissor globals + per-sprite clamp | The other clip mechanism (2D HUD/menu layer); well-matched already |
| 4 | Central export tables (`gUvGfxMgrExports`, `gUvFontExports`, `gUvSprtExports`, `gUvGfxStateExports`, `gSndExports`, `gScrnExports`) | Replace ad-hoc per-consumer `{char pad; fnptr unkNN}` shims with one shared typed header | Removes the biggest single source of illegibility across all modules |
| 5 | `scrn.c` (matched half) | Name a `Scrn_Exports` struct (array ptr, count, per-screen flag) | Screen/viewport manager; prerequisite for split-screen resize work |
| 6 | `gamegui.c` (~25 matched setters/getters) | Name `func_gamegui_*` handlers by the state they set | Most-touched HUD state machine; low effort, high clarity |
| 7 | `pause.c` (~29 matched) | Name nodes; verify `+0x6F98`/`+0x6F96` (candidate aspect fields) vs `gGameSettings` | Confirms or kills a possible aspect toggle; user-facing menu |
| 8 | `intro.c`, `logo.c` (fully matched) | Light pass — name export slots | Nearly done; cheap completeness |
| 9 | `selection.c` matched callbacks (~90) | Name by the global they set (`gCheatCars`, `gNumPlayers`, `gTransmissionType`) | Largest menu; callbacks are the legible part |

### Track B — Matching-decomp (unmatched `GLOBAL_ASM` → byte-exact C)

**Toolchain: STOOD UP + byte-verified; +57 functions banked (2026-07-01).** Built in **WSL Ubuntu-24.04**
at `/home/brysl/projects/bar-decomp`: IDO 5.3+7.1, splat asm (7346 `.s`), daisybox, `.venv`, baserom —
`make -j6` → `build/…z64: OK` (SHA `e5ab4d22`). A **parallel wave pipeline** banked **57 HUD/menu functions**
(448→391 unmatched, ~13%): `gen_cards.py` → **Sonnet 4.6** seed workflow → `consolidate2.py` (robust,
header-aware DaisyBox-hash byte-gate) → `capture_builderr.py` + `fix_wave` (Sonnet repairs compile errors
from the exact error text) → re-consolidate. All committed in the WSL clone; every wave rebuilds to SHA
`e5ab4d22`. Reusable scripts live in `lib/bar-decomp/tools/decomp_helpers/grind/`. Remaining tail: ~43
NOMATCH (**permuter** pool, `nomatch.json`), ~30 stubborn BUILDERR (Opus-tier), larger functions, and the
Phase-B scribe. Full resume: `/home/brysl/projects/bar-decomp/RESUME_GRIND.md`.

Ordered by letterbox relevance first, then small/easy modules:

| # | Target module / function | Size | Why this order |
|---|---|---|---|
| 1 | **`letter.c`** — 3 substantive funcs raw | tiny | Small, high-value shakedown target. Confirms the "not letterbox / Beetle collectibles" hypothesis byte-for-byte and retires a naming decoy. Ideal toolchain smoke-test |
| 2 | `cam.c` letterbox writers (`func_cam_00400190/00400C0C/00401D3C`) | medium | Pins the exact function/field that animates `unk218/21A` — the only way to be *certain* about the TOP/BOTTOM bar source |
| 3 | `scrn.c` (`func_scrn_0040009C`, `..._00400398`) | small (2/8) | Owns per-screen viewport rects; needed for split-screen resize; confirms no hidden scissor |
| 4 | `plyr.c` split-screen (`func_plyr_004044F8`, `..._00402340`) | medium (8/25) | Per-player quadrant rect computation — concrete split-screen coordinates |
| 5 | `filmroll.c` VI funcs (`func_00400588`, `osViBlack`×3) | small (2/9) | Ties into the RT64 present-mode / VI-origin work (R6 findings) |
| 6 | `menuslct.c` draw funcs (14 raw) | small (3/18) | Reusable widget; unlocks readable menu lists |
| 7 | `cbars.c` (`func_cbars_00400114`) | tiny (3/5) | Confirms color-bars test-pattern (kills the 2nd decoy) |
| 8 | `gamegui.c` HUD-draw core (`func_..._00400370/_014F8/_01748/_021DC/_023BC`) | large (26/38) | The actual HUD renderer; only place a HUD-local scissor could still hide |
| 9 | `selection.c` screen builders (~192 raw) | very large | Highest-value interactive surface, but biggest; do after framework is solid |
| 10 | `battle.c` (0/70) | very large | Split-screen viewport likely lives here; last, after `scrn`/`plyr` give the mechanism |

## Standing up the decomp toolchain (unblocks Track B)

Reference: `lib/bar-decomp/BUILDING.md` and the `Makefile`. In broad strokes, from `lib/bar-decomp/`:

1. **Baserom** — copy the SHA-correct ROM in as the expected baserom:
   `cp ../../build-cmake/bar.n64.us.z64 baserom.us.z64` (verify against `beetleadventurerac.us.sha1`).
2. **Python venv + deps** — `python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements` (splat, rabbitizer, mapfile_parser, m2c, decomp-permuter — several are vendored under `tools/`).
3. **IDO compiler** — the matching compile path is `asm-processor → IDO cc → as` (see `tools/ido-static-recomp`).
4. **Extract asm** — `make` runs splat to produce `asm/us/nonmatchings/...` (the `.s` referenced by every
   `GLOBAL_ASM` pragma) and does the first matching build.
5. **Confirm** — `scripts/check.sh` should print `…z64: OK` (ROM SHA-1 `e5ab4d22…`).

> Windows note: this path is fiddly (long paths, IDO, `mips-linux-gnu-as`). If it fights, run the
> decomp toolchain under WSL/Linux — the decomp is toolchain-portable; only the recomp is Windows-native.

## The per-function matching loop (Track B)

Follows `lib/bar-decomp/docs/AI_WORKFLOW.md` — the model-tiered write→verify pipeline:

```
score_functions.py → m2c (seed) → [writer model] → compile → ./diff.py -mwo <Func>
                         ↑                                        │ score 0?
                         └──────── decomp-permuter ←── no ────────┤
                                                                  │ yes
                                                          [Opus review] → name/type → commit
```

- **Gate is mandatory & tier-independent:** no merge without a verified byte-exact match.
- Route float-using / ≥80-instruction functions straight to Opus; escalate Haiku→Sonnet→Opus if not
  matched by attempt 3; `// MATCH: parked` after the budget and move on.
- Batch cheap writes, then **one** review + **one** checkpoint per module chunk (don't full-build per fn).

## The letterbox-modification workflow (recomp side) — IMPLEMENTED ✅ (Phase A, 2026-07-01)

The left/right pillars live entirely in the recomp/RT64, so this was buildable/verifiable with the
existing cmake build. A **`present_fill_mode`** control is now wired end-to-end:

- **Renderer** — `lib/rt64/src/render/rt64_vi_renderer.cpp` `fromHDtoWindow` takes a
  `PresentFillMode`: **Pillarbox** = uniform min-scale (original behavior — bars on the long axis),
  **Crop** = uniform max-scale + scissor clamp (fills, crops overflow, no bars), **Stretch** = per-axis
  (fills, distorts). `getViewportAndScissor` threads it; `render()` passes `p.fillMode`.
- **Config** — `PresentFillMode` enum + `presentFillMode` field added to both
  `RT64::UserConfiguration` (`lib/rt64/src/common/rt64_user_configuration.{h,cpp}`) and
  `ultramodern::renderer::GraphicsConfig` (`lib/N64ModernRuntime/.../config.hpp`), serialized as
  `present_fill_mode` in `graphics.json` (`src/game/config.cpp`), mapped by `to_rt64()` in
  `src/main/rt64_render_context.cpp`. Default = **Pillarbox** (zero regression). Live like `divot_option`.
- **Present** — `rt64_present_queue.cpp` reads `userConfig.presentFillMode` and honors a
  **`BAR_PRESENT_FILL=Pillarbox|Crop|Stretch`** env override (A/B testing).
- **Menu** — `<select id="pfm_option">` in `src/ui/assets/config.rml` ("Letterbox" row) + get/set in
  `src/ui/bar_ui.cpp`.
- **Testing aid** — **`BAR_WINDOW_SIZE=WxH`** (`src/main/main.cpp`) sets the initial window size so the
  non-4:3 modes are observable (the modes only differ on a non-4:3 window).

**Verified** (2026-07-01): built clean; captured the game window at 1280×600 — Pillarbox shows the
left/right pillars, Crop fills the window edge-to-edge (no pillars); the full `graphics.json → RT64`
path confirmed by tracing `present_fill_mode=Crop` through to the present renderer.

**Companion (optional, not yet changed):** to show *more scene* under Crop/Stretch instead of zooming,
pair with `ar_option=Expand` (widen 3D FOV, `src/game/config.cpp:130`) + HUD ratio Full
(`src/main/rt64_render_context.cpp:115-127`). Left at defaults to avoid changing everyone's FOV.

For the TOP/BOTTOM cinematic bars (separate mechanism), the runtime override is a clamp at
`lib/bar-decomp/src/modules/uvchannel_rom.c:445-447` (force `unk218=0`, `unk21A=screenH-1`) — poke it
via the SI/cheats RDRAM hook for a live toggle. (Not yet implemented; belongs to a later phase.)

## Verification cadence

| Scope | Command | Speed |
|---|---|---|
| One function | `./diff.py -mwo <Func>` | instant |
| One module chunk | `scripts/check.sh --module <m>` | seconds |
| Session / pre-commit | `scripts/check.sh` (incremental build + ROM SHA-1 + `progress.py`) | ~minutes |
| Recomp letterbox change | `cmake --build build-cmake` + `BAR_SHOTS` screenshot | ~minutes |

Keep commits small and per-function/per-module for easy bisection.

## Re-running / extending the map

The mapping workflow is saved at
`…/workflows/scripts/bar-hud-menu-map-wf_1ef2665d-ed1.js` (session workflow dir). Re-invoke with
`Workflow({scriptPath: …})` to refresh after new functions match, or edit the `MODULES` array /
`LENSES` to deepen a specific area (e.g. add a lens that reads freshly-extracted `cam.c` asm).
