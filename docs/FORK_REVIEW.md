# Fork improvement review and integration

Status: review and selected source integration **Done**, October 6, 2026;
release qualification **Partial**. The selected improvements and integration fixes are included;
the full Linux and Windows application builds, strict code-generation fixups and required MIPS patches pass. Actual boot, Controller Pak initialization, cached-ROM/Pak reload, widescreen driving and pause rendering have been exercised. Broader gameplay validation remains open. Do not publish this as a verified release.

Source: [Daniel Gomes Vieira's fork](https://github.com/danielgomesvieira2000/beetle-adventure-racing-recomp/tree/main),
revision `6497e672`. Common baseline: `a56fc350a13817b4e4f03666ccf07c03aa1ca77c`.
`git rev-list --count a56fc35..fork-review/main` reports **87 commits**. Review covers
the resulting source changes, including the surviving replacements for reverted experiments.
The accompanying [commit inventory](FORK_COMMIT_INVENTORY.md) records every commit's
disposition. Historical measurements in the imported technical chapters belong to Daniel's
fork; they are not claims that this integrated build has been tested locally.

## Improvements included

| Area | Included behavior | Integration corrections |
| --- | --- | --- |
| Frontend | RecompFrontend launcher, ROM selection, settings, controls, mods, icons and styling | Keep BeetleRecomp's application ID/config directory; retain cheats, keyboard defaults, saved graphics fields, bindings, named presets and player slots; expose per-port accessories |
| Saves/input | Controller Pak persistence, four-player SI/PIF input, rumble motor model | Validate ports/blocks/buffer bounds; preserve invalid existing saves; atomic save replacement and retry; report missing/unreadable Pak errors in both command formats; publish coherent input samples between threads; preserve distinct identical-pad identities on unplug |
| Widescreen | Viewport patches, framebuffer/scissor expansion, HUD placement, split-screen tiles/dividers, boot-picture classification | Original draw distance by default; culling follows the actual RT64 aspect ratio; validate crop/cover rectangles; restrict menu address tags to menu state |
| Renderer | Menu roll presentation, stretched framebuffer clearing, full-height framebuffer pairs, 16-bit/TLUT depth fog | Present cache belongs to its queue/device; inspector collection is synchronized; bounded diagnostic sets |
| Timing/audio | Attract-specific pacing, volume and focus muting | Keep game clock and audio routing separate from display interpolation; reject nonfinite volume; synchronize diagnostic counters between audio and game threads |
| Runtime configuration | Generic configuration and mod options needed by the frontend | Queue owned JSON/path snapshots rather than borrowed pointers; synchronize latest game mode; retain parsed control dependencies in the real schema; reject malformed roots/partial invalid enum lists/reversed numeric bounds; check completed writes |
| Build/release | Linux bring-up, MIPS viewport-patch pipeline, release packaging, technical reference | Preserve pinned runtime/renderer Git dependencies with reproducible parent patches; require generated patches in release CI; protect encrypted private build inputs and bundle licenses |

The old `src/ui` and its submodules remain available for comparison/recovery; the selected
build uses RecompFrontend. The fork's bulk replacement of every submodule with copied trees
is excluded. Only the newly introduced frontend is vendored, with its notices and licenses.
The existing recompiler pin is preserved with a reproducible parent patch for explicit
original SDK selection. The updated decomp passed its matching and Linux/Windows
code-generation/link gates; further accepted decomp waves still need repinning.
No game ROM, ELF, extracted asset, raw decomp snapshot or credential belongs in public Git.

## Deliberate decisions requiring very little human review

* **R1 — Wicked Woods door sound.**
  The fork changes one original sound parameter to stop the haunted-door loop. Its notes
  cite original-game evidence that the looping sound is an original defect. This is a
  fidelity choice, so the behavioral change is excluded from the integration.
  * **A (recommended):** Keep the original behavior, as currently implemented.
    *Impact:* preserves hardware behavior and avoids changing an audio parameter whose other effects are not fully established.
  * **B:** Add a separate, default-off enhancement after a course/audio test.
    *Impact:* gives players a fix without silently altering original behavior; requires one small implementation and gameplay check.

* **R2 — Extended draw distance.**
  The fork reports disappearing dynamic objects above 1.5x on Wicked Woods, with the root
  cause unresolved. Higher values on other courses lack comprehensive verification.
  * **A (recommended):** Keep 1x as the default; larger values are opt-in and Wicked Woods remains capped at 1.5x.
    *Impact:* retains the enhancement while protecting normal play from an unresolved object-submission failure.
  * **B:** Hide all multipliers above 1x until the object-drop cause is fixed.
    *Impact:* removes experimental rendering choices, but avoids the known failure entirely in normal settings.

## Verification evidence and remaining gates

Windows CMake configuration with clang-cl and the frontend enabled succeeded. Direct,
sequential compilation checks cover the changed host, frontend, runtime configuration,
input, HUD, VI, presentation and projection code. This is compilation evidence, not a
full link or game test. `scripts/test-runtime-input.ps1` runs only isolated fixtures and
SDL virtual pads: save bounds, damaged-save preservation, failed-write retry, stable pad
identity after unplug, unassigned slot gaps (including port one) and separate imported keyboard/controller profiles.
`scripts/test-si-pak.ps1` exercises the actual SI handler with isolated emulated memory:
all four ports, short/padded read/write commands, missing controllers/accessories,
unreadable saves, preserved response lengths, one completion per command and DMA bounds.
`scripts/test-mod-dependencies.ps1` checks the actual manifest dependency parser and
configuration updates without loading files or mods. The frozen pre-fix source fails
with `parsed disabled dependency was discarded`; the corrected source passes. Numeric
bounds and partially invalid enum dependency lists are also covered. This correction
was included in the hash-checked retry bundle and passed the complete Linux build.
All three regression scripts now compile into independent scratch directories rather
than replacing application objects. The isolated mod, SI and input suites passed
sequentially after this correction. Test failures cannot leave pre-fix objects in the
application build directory.

`tests/test_dependency_patches.py` checks all three parent patch sets against pristine pinned
dependency files, including apply and reverse checks. This must be rerun whenever a patch
is regenerated. Setup scripts apply these reviewed patches without resetting user changes.

The complete Linux Release build passed with Clang 18.1.3, the frontend enabled and
`BEETLE_REQUIRE_PATCHES=ON`, using four jobs in isolated pve CT 105. Main and patch
code generation, strict fixups, MIPS patch compilation/linking and license staging
also passed. The frozen correction bundle SHA-256 is
`e1d91fd599706b808aa8621741bf5efc9bbda4bf901ec1693f5e9a21e599aab4`;
worker evidence is under `/opt/beetle-recomp-fix-20261006` (`source-check.log`,
`configure.log`, `build.log`, `licenses.log`) and `/opt/beetle-recomp-review-20261006`.
The Linux wrappers now default to four jobs and require the viewport patches.

The later decomp ELF (`07b1fdf9853f11d56ec59c1099a722db9794b965d81c909656ef3fee8d057a79`)
also passed main/patch code generation, every strict fixup and the full Linux Release
application link. Updating that dependency exposed three compatibility defects, now fixed:
fully recovered modules were omitted from the decomp's recomp linker; fixups assumed old
overlay addresses and section numbers; the recovered boot code called an unbound raw
cartridge-read SDK function. Module discovery now includes all 133 C modules; fixups
resolve addresses, function names and section indexes from the exact ELF and verify
original instruction words; all three PI read entrypoints now read big-endian ROM words
with alignment, device, ROM and RDRAM bounds checks. The previously empty read stubs are
removed. Actual-production-code PI regressions passed on Windows and Linux. The fixup
regression passed with both old and new ELFs, rejecting a changed original instruction
and resolving synthetic rebased modules. Evidence:
`/opt/beetle-recomp-review-20261006/cartridge-read-validation/{build,pi-read-test,anchors-test}.log`.
The address resolver uses Python's standard library and adds no package dependency.

The patch Makefile now records included-header dependencies and relinks when either
linker script changes. Previously an incremental build could retain stale MIPS patches
after a decomp/header update. `tests/test_patch_dependencies.py` exercises both cases
against the actual Makefile. The current pinned decomp headers are transferred with
their source hashes and used for the patch validation.

The final integrated private ELF is SHA-256
`637ac32a1e7c4cb42536342ee72fee2bcf71bca9933acdb1026ec75c7818abd6`,
from decomp revision `26674c5a36073578a933e12f45c581ea145c0855`. It includes 21
additional readable functions beyond the verified upstream integration. Main/patch
code generation, every strict fixup, all 27 original SDK routines, current decomp
headers and complete Linux/Windows application builds passed. Evidence:
`/opt/beetle-recomp-review-20261006/current-headers-validation`. The 74 generated
inputs were transferred privately with archive and per-file SHA-256 verification;
previous local outputs were retained for recovery. These private inputs are ignored by Git.

### Windows build and actual game checks

The Windows cross-build passed using Clang 18, the user's existing VS2022 14.42 libraries
and Windows SDK 10.0.26100. Three shader libraries were compiled with the existing native
Windows DXC 1.7 in under two seconds; substantial C++ compilation/linking ran on pve.
Linux and Windows DXC embed different version strings even at the same release, which
prevents linking the Linux-generated shader libraries with the Windows DLL. This is an
isolated cross-build adaptation; ordinary native Windows builds retain their normal path.
No downloaded compiler or SDK is added to the repository.

Fresh launcher rendering passed at 640x480, 1280x720 and 1720x720 after correcting the logo
layout. The new decomp symbols exposed another compatibility defect: automatic SDK binding
selected host Controller Pak stubs. N64Recomp now supports an explicit `patches.original`
selector; BAR retains 27 original Pak/motor routines and their required helpers. The
selector rejects missing and duplicate functions. All 27 generated bodies and original
call routing pass `tests/test_original_sdk.py`; main/patch generation, strict fixups and
complete Linux/Windows builds pass after the correction.

Actual runtime checks created a 32 KiB Pak containing BAR's `NNSE` game entry, passed the
previous peripheral warning, and reached Coventry Cove. A second run loaded the cached
ROM and existing Pak, preserved its SHA-256, drove the car at 80 km/h with expanded 16:9
rendering, and opened the original pause menu. It remained running through a 190-second
observation with audio enabled; audible quality was not independently reviewed. App-rendered
screenshots and logs are private under `.review/original-controller-sdk-complete`.

Closing during gameplay correctly opens the frontend's quit-confirmation dialog, captured
in `race-persistence-test/quit-confirmation.png`. The test then deliberately terminates the
process without accepting that dialog. Its exit code 1 is not a demonstrated shutdown hang;
a clean exit after accepting the prompt is still unverified. The fresh launcher exits cleanly.
An eight-second launcher reload also loaded all five saved/imported control profiles,
preserved `controls.json` byte for byte and exited with code zero.

The final ELF also exposed a fast-start defect in the host VI initialization. A cached
game can start before the first timed video interrupt; its graphics module then reads
current framebuffer zero instead of the required `0x100000` and never unblanks. Paired
debugger captures established those different return values even though all 3,146
generated game functions remained equivalent after symbol renaming. Initialize both
VI states synchronously before starting renderer/task threads. Protect retrace state
swaps and framebuffer getters with the same mutex used by SDK state changes, preventing
lost mode, framebuffer or blanking updates.

The corrected Windows executable returned `0x100000` at the first graphics-module
check and rendered the Controller Pak menu in five independent cached starts; all five
captures have identical SHA-256. A 190-second expanded 16:9 run reached Coventry Cove,
drove at 111 km/h, rendered pause and preserved the existing Pak hash. An earlier run
closed by the user exited with code zero and also preserved the Pak. Audio was enabled;
audible quality remains unreviewed. Complete Linux and Windows builds and pristine
apply/reverse checks for all three dependency patches passed. All 14,114 frozen source
and private generated-input hashes agree locally and on the validated worker. Evidence:
`/opt/beetle-recomp-review-20261006/vi-startup-validation` and local
`.review/vi-startup-validation/{boot-framebuffer-debug,repeated-fast-starts,race-persistence-retry}`.

Remaining release gates: actual settings/import behavior;
single-player and 2/4-player input/saves/rumble; resize/Original/16:9/ultrawide HUD and split
screens; boot splash, menu roll, pause, Mount Mayhem fog, attract pacing/audio and results
transitions. Test motion and repeated transitions, not only screenshots. Preserve logs and
source hashes. Heavy builds belong on the isolated pve worker under the user's fleet policy.

The hang watchdog is opt-in (`BAR_WATCHDOG=1`): its precautions cannot guarantee that
DbgHelp avoids allocations while another thread is suspended. This is a diagnostic tool,
not a verified hang repair. The fork's unresolved results-transition issue still needs the
runtime gate above; no new diagnosis has been claimed here.
