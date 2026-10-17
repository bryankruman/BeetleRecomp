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

## Latest runtime qualification — October 11, 2026

The two production fixes are committed locally at
`8a7370e4c41ea8474c6cfd920926c5e06b20e18a`: stylesheet loading reads bounded chunks
and reports stream failures; `BAR_TEST_QUIT_AFTER_MS` enables an opt-in, bounded
shutdown test. Both complete translation units compiled on the isolated worker.
The stylesheet stream cases also passed the focused Linux regression; Windows
error-dialog behavior is not established by that regression.

A separate Windows fixture passed the normal confirmed-shutdown path in 45.77
seconds with exit code zero and no harness termination. It queued `SDL_QUIT`,
opened the normal confirmation prompt, invoked the existing confirmed-Quit
callback, and returned through runtime shutdown in that order. All original and
fixture file guards stayed unchanged. The fixture includes one additional
opt-in confirmation hook in `ui_state.cpp`; that hook was **not** integrated into
production. This qualifies the in-process callback path, not a physical click.
Private receipt: `windows-confirmed-quit-actual-9838.json`, SHA-256
`665172d2abd3b0c079413f2467dadcefd319580b24b8f7beb22fd2ee368c3608`.

The separately recorded six-minute gameplay run rendered steering responses,
pause with an unchanged race timer, and resumed timer advancement. Its harness
ended the run; it is not shutdown or completed-lap evidence. Isolated host tests
also passed settings/profile persistence and four virtual-controller/Pak paths;
they do not establish rendered multiplayer or physical rumble.

Release qualification remains partial: rendered multiplayer/split screens,
live settings/import and resizing, course-specific fog, completed laps/results,
audible quality and repeated end-to-end transitions still need qualification.
The source integration is complete; no release has been published. Preserve the
two fidelity choices recorded as R1 [Wicked Woods door sound] and R2 [Extended draw distance].

## Prior paired build validation — October 10, 2026, 11:58 PM ET

The isolated Linux application build passes against the accepted decomp commit
`f66abae11ee5899e276ce27f8ee22aba676ca75c`, including 140 recovered SDK functions.
All nine checks passed: anchor regression, main code generation, strict fixups,
fixup idempotence, symbol generation, MIPS patch build/generation, configuration
and the complete frontend build. The four-job compile/link step took 190.48 seconds
on the isolated pve worker. All 14,114 original frontend/private-input files,
1,156 decomp source files, original ROM and previous builds passed their guards.

This supersedes earlier paired ELF/build hashes for current build compatibility.
ELF SHA-256: `15b44b2884154a4bb29088aaf0912ae897ab197f23eb35182d68daa65a6f15ef`.
Linux executable SHA-256: `88a0f89fc8d4a6a33fc67f1d8d4cef85069ffebf46de558b24556196c9261779`.
Private receipt: `ordinary27-controller140-paired-actual-4952.json`, SHA-256
`3885e2a40652c434323aa000719cf7e278947439c727e9ff6c3120b4971a4684`.
The 21 explicit original helper boundaries and authoritative recompiler configuration
remain unchanged. The new binary has not passed interactive runtime qualification;
previous Windows/gameplay observations apply to their recorded older executables.
Settings/import, multiplayer, saves/rumble, live resize/split screens, course fog,
audible audio quality, pacing, results transitions and repeated normal quitting
remain release gates. Fork integration is complete; release qualification is partial.

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
that run did not verify a clean exit after accepting the prompt. The later in-process
confirmed-Quit check is recorded above. The fresh launcher exits cleanly.
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

Additional bounded runtime observations used the same validated Windows executable
(`fa98c8d5670e181092895a3a3f2abe73d6548b7fa55901e7e3d0fb9473b727a9`).
A four-minute original-aspect attract run rendered several cinematic scenes, menus
and a forest/bridge attract scene. Eight captures and an 87.38-second raw stereo
audio sample are retained privately. Nonzero raw samples establish audio production;
they do not certify audible playback or pacing. One cinematic capture contains sky
edge fragments; compare motion and the original game before calling this a regression.
Evidence: `.review/vi-startup-validation/original-aspect-attract-3070/review-3076.json`.

Two subsequent 140-second startup-viewport runs exercised Original aspect in a
1280x720 window and expanded rendering in an 860x360 window. All ten captures were
visually reviewed. Both reached Coventry Cove racing with readable HUD, minimap and
gauge; Original aspect retained side bars and expanded rendering extended the scene.
Both preserved the private Pak hash. The harness deliberately terminated its own
live processes: exit code 1 is neither a crash finding nor a normal-quit check.
These observations do not test live resize, multiplayer or split screens.
Evidence: `.review/vi-startup-validation/viewport-matrix-3078/review-3091.json`.

Remaining release gates: actual settings/import behavior;
single-player and 2/4-player input/saves/rumble; resize/Original/16:9/ultrawide HUD and split
screens; boot splash, menu roll, pause, Mount Mayhem fog, attract pacing/audio and results
transitions. Test motion and repeated transitions, not only screenshots. Preserve logs and
source hashes. Heavy builds belong on the isolated pve worker under the user's fleet policy.

The hang watchdog is opt-in (`BAR_WATCHDOG=1`): its precautions cannot guarantee that
DbgHelp avoids allocations while another thread is suspended. This is a diagnostic tool,
not a verified hang repair. The fork's unresolved results-transition issue still needs the
runtime gate above; no new diagnosis has been claimed here.

### Current Windows shader-bank build and qualification

The current Windows production executable is SHA-256
`1132776a4e859e1ccdf939ec5175a79f475a57ca495b961150c0dbc8fe7d8f8b`.
The full four-job Windows cross-build and its provenance checks passed. This build
retains the reviewed SDK251 private generated inputs; it does not claim
regeneration from the current decomp checkout. The eight
tracked build-route changes are committed locally at
`326d167a916877abec7e1eb7123152a0f2df2754`; the existing dependency modifications
remain preserved. The tracked packaging procedure produced a private ZIP, SHA-256
`890e0cc9baf88b159e4896e9b75fb34cde3febd28b90e6295d09930e2b8f0083`.
Neither the commit nor the package constitutes publication or runtime approval.

The earlier explanation about differing Linux and Windows compiler version strings
was incomplete. The failing build embedded shader libraries produced by Linux DXC
1.8 while the packaged Windows compiler libraries were DXC 1.7. That incompatible
shader/compiler lineage caused the runtime shader-compilation failure. The corrected
bank contains the three libraries rebuilt with the existing native Windows DXC 1.7,
with source/include, tool and DLL hashes recorded. The optional cross-build route
uses `BEETLE_WINDOWS_DXIL_BANK` and `BEETLE_HOST_FILE_TO_C`; it validates the bank and
rebuilds the consumer that depends on the library sizes. Ordinary native Windows
builds retain their existing route. No compiler download is required.

The new production executable rendered the Controller Pak game menu in a bounded
test and exited naturally with code zero after the frontend's Quit confirmation.
Its runtime-file, source, history, backup and ROM checks passed. The earlier Coventry
Cove race and viewport observations for executable `fa98c8d5…` remain historical
evidence for that older build; they do not qualify gameplay on the current executable.

A separate private diagnostic executable, SHA-256
`886beff7d0c6dacc94ecc6b698226125260cbaadef07ac9d8caf62ce1f0c9a77`,
recorded 219 keyboard-diagnostic rows, below the 384-row cap, and also exited naturally
with code zero. Down-key press and release events reached the render queue within
one polling interval, while the sampled game keyboard state remained neutral.
The test did not establish delivery of the attempted X and Escape inputs. These
observations do not establish a production input defect or successful game control.
The diagnostic instrumentation is detached from the production source and package.

Current release qualification remains incomplete. The new production build still
needs verified sustained driving, physical player input, saves/reload, multiplayer
and rumble, audible playback, live viewport/aspect changes and repeated pause/results
transitions. Actual boot and clean quit are verified separately from these gates.

The following private local receipts preserve the exact build and observation lineage;
they contain no claim of public release or completed gameplay qualification.

| Receipt | SHA-256 |
| --- | --- |
| `recomp-native-bank-53202-commit.json` | `e1bc3883972217a93d318cda3637ad6c8f0ac18f823c472c4cc3bd5e5f2535dd` |
| `native-bank-cmake-build-actual-52718.json` | `18ff5433f6646f4ba2214bcc9399a719db2197290df27df7fa8cdd29ad450885` |
| `native-bank-private-package-actual-53128.json` | `cc1bab3465aba5471ce3f1c2622368f3579b8040dd2bef3098fd009c952dc640` |
| `SDK251-native-bank-production-runtime-53518.json` | `e00c7524302f03aa8d07bad88285b0c4dfc39ac7064484c1a49f9ea804801fd3` |
| `SDK251-native-bank-key-runtime-53718.json` | `c35bcd8878df1fb31a80088d34f34fda6603faf7529cd32c14480b50015b630d` |
| `SDK251-native-bank-key-runtime-53718.key-diagnostic.json` | `c7570bfa2798f761bad2bd803404608d1d3fc57783273588a59618d0ec07d181` |


## Mod dependency fixes and production build — October 17, 2026

Two additional runtime dependency defects are fixed in local commit
`536e0183e80b349f964aaf9db229d223927c6cba`. Auto-enablement now checks the
installed dependency's version against its deprecation cutoff, instead of
checking the requested minimum version. Runtime activation also invokes the
dependency's own content handlers with its own `ModHandle`, instead of repeating
the parent handlers and passing the parent handle. Missing-ID bookkeeping remains
available to the existing load-time dependency validator.

The canonical parent dependency patch contains both changes; upstream submodule
HEADs and gitlinks remain pinned. Original source and patch backups are retained
privately. The reusable regression reads complete current production methods and
the `Version` definition each time; it does not keep duplicate implementations.
Run it with `python scripts/test-mod-enable.py --cxx g++` using an existing C++20
compiler. See [the focused test notes](../tests/README.md) for platform flags and
the state-fixture limitations.

On the isolated worker, the original production-method fixture failed at the
installed-version regression; the version-only fixture failed at the callback
identity regression; both corrections passed together. The final portable runner
also passed with g++ 13. Ten mocked process/argument controls cover the runner;
the Windows job-object branch has not been executed on Windows.

The corrected complete `mods.cpp` translation unit additionally compiled with
the retained clang-cl 18 Windows recipe and real headers, producing a
9,212,321-byte AMD64 COFF object. All 14,274 historical source files, 1,187 build
files, five tools, SDK overlay and pinned inputs stayed unchanged before and
afterward; the compiler's owned process group was absent afterward. Only a new
scratch object was produced: no historical object, library or executable was
replaced. Private receipts are `recomp-both-mod-regressions-actual-68550.json`,
`recomp-portable-mod-runner-actual-68570.json` and
`recomp-mods-production-TU-actual-68582.json`.

The corrected object subsequently passed the whole Windows application link in
an isolated directory. The new `librecomp.lib` preserves all 29 archive members
in their original member order: 28 payloads are unchanged and the sole replacement
is the qualified `mods.cpp` object containing commit `536e0183`'s fixes. One archive
command and one application link passed; both owned process groups were absent
afterward. All historical source/build files, SDK inputs, tools and referenced
inputs passed independent final guards. No historical output was replaced.

The resulting AMD64 Windows executable is 19,928,064 bytes, SHA-256
`9c2a4d5e6e74998c03aaa233187d0e9757b8a313ee595e06c17765a015a28c71`.
Private receipt: `recomp-mod-fix-whole-app-link-actual-68678.json`, SHA-256
`4349c6f20172d022e78bf959ef17ef1d3afb877f7bed16cb4cd9ffb6fe83f5d3`.
The link retains the qualified native Windows DXC 1.7 shader library and the
original SDK251 generated game code. It does not regenerate or qualify the latest
decomp checkout. This executable has not been launched; startup, runtime and the
remaining gameplay gates above remain open for this build.

The public fork's main revision was rechecked on October 17 and remains
`6497e67257809401468cd47850393775952b31bb`; there is no newer fork source revision
to integrate at that observation.
