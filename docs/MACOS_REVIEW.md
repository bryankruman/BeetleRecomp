# Apple Silicon compatibility review

Status: source fixes and portable regression checks completed; native macOS build,
packaging and gameplay qualification are pending. No M1 executable was produced
by this Windows-hosted review. A full-featured Mac release is not yet certified.

## Fixed issues

| Area | Defect and correction |
| --- | --- |
| Window / Metal | The host returned SDL_Window* for an Apple WindowHandle containing two native pointers. It now obtains the Cocoa window, creates an SDL Metal view on the main thread, and passes its CAMetalLayer to RT64. The view lives until renderer threads have joined. Headless renderer field conversion is fixed too. |
| Apple build | Enable Objective-C/Objective-C++ at the parent CMake scope, select the native host DXC for frontend shaders, link/include SDL and Cocoa, and create a real app bundle. |
| Finder launch | Stage fonts, styles and icons under Contents/Resources/assets, matching NSBundle resource lookup. |
| Retina menus | Enable Plume backing-pixel dimensions to match the frontend, convert SDL logical mouse and resize coordinates to pixels, and update hover position on a click even without a preceding motion event. Scroll distances are unchanged. |
| Saves and settings | Resolve the Mac executable with _NSGetExecutablePath; use Application Support for a new installation, retain existing XDG/.config directories, and recognize portable.txt beside a standard .app. Resolve the account home if HOME is missing. No existing save is moved or rewritten by this selection policy. |
| Controller template text | Command-based copy, paste and select-all now reach the text widget's shortcut modifier. Gameplay mappings are unchanged. |
| Optional code mods | Cover the entire instruction patch across page boundaries, check memory-protection errors, use private-copy Apple page transitions, and invalidate the instruction cache after patching and restoring code. This is supplied through the tracked dependency patch installer. |
| Portable package | A separate packager relocates dynamic dependencies into a fresh app, checks ARM64 and internal dependency paths, derives the minimum OS from the linked images, collects notices, and ad-hoc signs. It never builds, publishes or notarizes. |

The restored controller templates, default mappings, automatic discovery,
assignment policy, Controller Pak and rumble paths use the same SDL-backed code
on Mac. The new-menu configuration normalizes copied D3D12/Vulkan settings to
Auto before renderer construction because only Auto and Metal are offered on Mac.
No controller, cheat, widescreen or frame-interpolation feature was intentionally
removed for this port.

## Verification performed

- Controller/default/assignment tests, including Mac shortcut, bundle path and
  existing-save-directory cases: pass.
- Production profile/save regression tests using isolated files and virtual
  controllers: pass.
- Retina motion, clicks, resize, fractional scales, scrolling and invalid-size
  guard tests: pass.
- CMake host compiler selection and missing-tool rejection, packaging path and
  minimum-OS parsing, and app bundle contract tests: pass.
- Extracted production code-memory helpers with mocked Apple, POSIX and Windows
  APIs: pass. These do not modify executable pages or prove macOS signing behavior.
- Changed host, renderer, UI state, config and SDL text-backend translation units:
  compile under the existing Windows toolchain. Apple-only branches still require
  the real Apple SDK. Dependency patch application is idempotent.

Early local test invocations failed from an incorrect SDL include path and the
existing clang/SDL prefetch-header guard; corrected invocations passed. Native
Apple compilation was not substituted with a simulated platform build.

## Native acceptance checklist

Use BUILDING.md for native build and optional portable packaging commands. Keep
private generated game code on the owner's machines. No ROM or ELF goes into a
published package. A Linux worker cannot validate Cocoa or the Metal GPU backend.

| Check | Required result |
| --- | --- |
| Clean ARM64 build | Full application links with generated viewport patches and correct shaders; executable and every bundled dylib contain arm64. |
| Fresh Finder launch | App opens from a directory unrelated to the checkout, shows all text/icons, accepts the owner's ROM through the native picker, and relaunches using the cached copy. |
| Retina / displays | Mouse and trackpad hit the displayed controls after resize/fullscreen and moves between Retina/external displays. Native file dialog returns correctly. |
| Controller templates | Actual intended USB/Bluetooth pad gets defaults, every N64 input works, save/apply/rename/delete persists, and reconnect retains independent mappings. Test two pads and deliberate player gaps if multiplayer is required. |
| Accessories and saves | Rumble works on the selected controller/transport; Controller Pak data and normal game progress survive restart. Check existing-save and fresh-install paths separately. |
| Gameplay / audio | Start and finish a race, verify menus, music and effects, pause/resume, focus changes and clean exit. Check suspend/resume and audio device changes separately. |
| Enhancements | Widescreen, HUD placement, draw distance, cheats, interpolation and resolution changes behave as documented; restart-required settings take effect after restart. |
| Mods | Test any intended texture packs/code mods, including load and removal. Native mod dylibs need ARM64 builds. Real code-page permission/signing behavior remains an explicit gate. |
| Portable artifact | Copied package launches without the build machine's Homebrew paths; signatures and minimum OS check pass. |

A short functional smoke test can establish input/menu correctness. Sustained
frame rate and heat on the fanless M1 Air need a separate representative race;
passing a short test does not establish sustained performance. Native-resolution,
MSAA and supersampling limits depend on the GPU and chosen settings; unlimited
frame rate or every quality setting at maximum is not a compatibility promise.

The user's actual macOS version and controller models/transport were requested
and remain to be recorded. No remote Mac access was established in this review.
The decomp campaign and its automation remain paused.

## Primary references

- SDL Metal view/layer ownership: https://wiki.libsdl.org/SDL2/SDL_Metal_CreateView
  and https://wiki.libsdl.org/SDL2/SDL_Metal_GetLayer
- SDL window-relative mouse event coordinates:
  https://wiki.libsdl.org/SDL2/SDL_MouseMotionEvent
- SDL rumble reports unsupported controllers explicitly:
  https://wiki.libsdl.org/SDL2/SDL_GameControllerRumble
- Apple's controller support and per-device feature limitations:
  https://support.apple.com/en-us/111099
- RT64's upstream Metal backend: https://github.com/rt64/rt64

References support the platform design, not a claim that this BAR build has
already passed the native checklist.
