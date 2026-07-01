# Seam Fix — see-through seams between world-geometry planes (TODO #2)

_Created 2026-06-30. Implementation plan only — no code changed yet._

This is the detailed plan for [docs/TODO.md](TODO.md) item 2. It covers the confirmed root cause,
the two fixes we're shipping (**A. MSAA + Supersampling**, **B. VI "divot" filter**), the exact
code changes for each (with file:line), and the Settings-menu controls for both.

---

## 1. Root cause (confirmed)

Adjacent/abutting (often coplanar) world-geometry triangles show a thin see-through crack at their
shared edge, revealing the background/skybox. This is a **well-known N64 HLE artifact**, not unique
to BAR, and it has two halves:

1. **The N64 RDP fills shared edges with sub-pixel _coverage_, not a fill rule.** The RDP rasterizes
   on a fixed-point grid (X = s15.16, **Y = s11.2** — only 4 vertical sub-pixels) and stores a 3-bit
   per-pixel coverage value. Two abutting triangles write the shared boundary pixel exactly once via
   _complementary_ coverage — seamless. An HLE renderer transforms vertices in **floating point** and
   hands them to the host GPU rasterizer, whose fill rules reproduce none of this; when float rounding
   places the shared edge on a slightly different sub-pixel, the edges no longer tile and a crack
   opens. Proven by a GLideN64 dev forcing angrylion's accurate RDP into non-AA mode: "gaps indeed
   appeared" ([GLideN64 #2397](https://github.com/gonetz/GLideN64/issues/2397)).
2. **Real hardware hides the residual holes with the VI "divot" filter — HLE omits it.** From
   Nintendo's VI manual, verbatim: a filter for eliminating "single-pixel holes created when multiple
   boundary edges overlap in one pixel… **one-pixel holes will appear where polygons are connected
   together**" ([Nintendo VI manual ch.29](https://ultra64.ca/files/documentation/online-manuals/man-v5-1/pro-man/pro29/29-01.htm)).
   It is a per-channel **median-of-3** applied at scanout. LLE renderers (angrylion, parallel-RDP)
   implement it and don't show the bug; HLE renderers don't and do.

**Upscaling is an amplifier, not the cause.** Our default is native rasterization (see §2), so the
holes are ≤1 native pixel — but with no divot they still show, then get magnified by the final
window scale-up. The same family of report exists for RT64-based ports; the Zelda64Recomp community
ships a content-side ["Seam Fixer" mod](https://thunderstore.io/c/zelda-64-recompiled/p/Reonu/Seam_Fixer/)
because RT64 is deliberately faithful and does not paper over these.

**Diagnostic that scopes this plan:** a see-through crack _to the background_ = the coverage/edge
mechanism above. That is distinct from decal/Z-fight _flicker_ (surfaces poking through each other),
which is a different fix (RT64 `gEXVertexZTest`, depth bias) and explicitly **out of scope** here.

### Why these two fixes

| | What it does | Nature |
|---|---|---|
| **A. MSAA + SSAA** | Adds edge coverage (MSAA keeps ~30%-covered edge pixels the plain rasterizer discards) and supersamples thin geometry, **softening/reducing** seams. | Mask — cheap, uses knobs we already have. |
| **B. VI divot filter** | Reconstructs the hardware concealer: a horizontal median-of-3 at native-pixel spacing that **fills** the 1-pixel holes. | The faithful fix — small, self-contained, we have the RT64 source. |

A masks broadly and instantly; B targets the exact holes. They compose (SSAA feeds a cleaner image
into the divot). Ship A first (trivial), then B.

---

## 2. Where our renderer sits today (verified)

- **Default = native rasterization.** `default_graphics_config()` sets `res_option = Resolution::Original`
  and `ds_option = 1` ([config.cpp:124,133](../src/game/config.cpp)). `Resolution::Original` maps to
  RT64 `Manual` with `resolutionMultiplier = downsampleMultiplier = max(ds_option,1) = 1`
  ([rt64_render_context.cpp:85-89](../src/main/rt64_render_context.cpp)) — i.e. render at 1× native,
  no supersample. (The earlier survey's "Auto/upscaled default" was wrong.)
- **MSAA default = None**, but the menu control already exists. `c.msaa_option = Antialiasing::None`
  ([config.cpp:129](../src/game/config.cpp)); `<select id="msaa_option">`
  ([config.rml:98-103](../src/ui/assets/config.rml)); wired in
  ([bar_ui.cpp:149,168](../src/ui/bar_ui.cpp)).
- **Supersample (`ds_option`) exists + persists but is _not_ in the menu.** It's serialized
  ([config.cpp:79,97](../src/game/config.cpp)) and drives `resolutionMultiplier`/`downsampleMultiplier`
  ([rt64_render_context.cpp:87-93](../src/main/rt64_render_context.cpp)) — that's true SSAA (render
  high, downsample) — but there's no control in `config_to_controls`/`controls_to_config_and_apply`
  ([bar_ui.cpp:144-178](../src/ui/bar_ui.cpp)).
- **MSAA + resolution/SSAA apply on _restart only_.** RT64's runtime `updateMultisampling()` crashes,
  so we snapshot `m_live_aa`/`m_live_resolution`/`m_live_res_mult`/`m_live_down_mult` after setup and
  re-assert them on every live config change ([rt64_render_context.cpp:173-176,302-309](../src/main/rt64_render_context.cpp)).
  The chosen values still persist and take effect next launch through the validated setup path.
- **No divot filter runs.** The VI status `divotEnable` bit is parsed ([rt64_vi.h:32](../lib/rt64/src/hle/rt64_vi.h))
  but only shown in the debugger — nothing in the render path consumes it.
- **The VI present path** ([rt64_present_queue.cpp:306-349](../lib/rt64/src/hle/rt64_present_queue.cpp)):
  the game framebuffer is resolved/downsampled to native (`renderParams.texture`,
  `textureWidth/Height` = native res), then `viRenderer->render(renderParams)` draws it to the
  swap-chain via [VideoInterfacePS.hlsl](../lib/rt64/src/shaders/VideoInterfacePS.hlsl). The VI pixel
  shader samples `gInput` at **input-texel granularity** (`textureResolution` = native fb size), so a
  divot median sampling `uv ± 1/textureResolution` reads true **native-pixel neighbors** — exactly
  where the holes live — regardless of output/window resolution.

---

## 3. Option A — MSAA + Supersampling (menu + defaults)

Mostly exposing/defaulting existing knobs. Three small edits.

### A1. Default MSAA → 4×
- [src/game/config.cpp:129](../src/game/config.cpp): `c.msaa_option = Antialiasing::None;` →
  `c.msaa_option = Antialiasing::MSAA4X;`
- Safe: defaults flow through RT64's validated setup path; setup downgrades to None if the device
  can't do 4× and we snapshot whatever it chose (`m_live_aa`). Menu control already exists. Mirrors
  Pokemon Stadium Recomp (RT64 port) which defaults to 4× MSAA.
- Cost: modest at native res; user can lower it in the menu.

### A2. Expose Supersampling (`ds_option`) in the menu
`ds_option` is an `int` multiplier (1 = off, 2 = 2×, …). Treat it like `rr_manual_value` (the other
int control), not an enum.

- **[src/ui/assets/config.rml](../src/ui/assets/config.rml)** — add a row after the MSAA row
  (after line 104):
  ```html
  <div class="row">
      <span class="label">Supersampling (SSAA)<span class="sub">Applies on restart</span></span>
      <select id="ds_option">
          <option value="1">Off</option>
          <option value="2">2x</option>
          <option value="4">4x</option>
      </select>
  </div>
  ```
- **[src/ui/bar_ui.cpp](../src/ui/bar_ui.cpp)**:
  - `config_to_controls()` (after line 152): `set_control_value("ds_option", std::to_string(g_working_config.ds_option));`
  - `controls_to_config_and_apply()` (near the `rr_manual` stoi block, lines 172-174):
    ```cpp
    try {
        g_working_config.ds_option = std::stoi(control_value("ds_option", std::to_string(g_working_config.ds_option)));
    } catch (...) { /* keep previous */ }
    ```
- No `config.hpp`/`config.cpp` change — `ds_option` already exists and is already persisted.
- **Default stays `1` (off).** SSAA is expensive (2× = 4× fill rate); MSAA4× is the cheaper default
  win from A1. Leave SSAA opt-in via the menu.
- Note: `ds_option` feeds the resolution/downsample multipliers, which are under the `m_live_*` lock,
  so SSAA — like MSAA — **applies on restart** (the row says so).

### A3. (Optional, defer) Expose `res_option` clarity
The `res_option` control already exists (Original / 2× / Auto). No change needed; just be aware 2×/Auto
move to upscaled rasterization, which can _widen_ remaining seams — the divot (B) is what fixes those.

### A — testing
Build, launch, navigate **into a track** (per the "verify on the actual screen" rule — idle is the
attract loop, not gameplay). Compare a known seam: default (now MSAA4×) vs None, then SSAA Off vs 2×.
Expect softening, not elimination — elimination is B's job.

---

## 4. Option B — VI "divot" filter (the faithful fix)

Reconstruct the hardware divot as a **per-channel horizontal median-of-3 at native-pixel spacing**,
folded into the existing VI pixel shader. No new render pass, pipeline, or shader file — the VI shader
already samples the input at native-texel granularity, so this is a contained, low-risk change that
runs at the correct resolution and composes with all filtering modes.

### B0. Game's divot bit — ✅ VERIFIED (static, 2026-06-30): unconditionally ON
BAR installs a **LAN1** VI mode (low-res antialiased): `OS_VI_NTSC_LAN1` / `OS_VI_PAL_LAN1` /
`OS_VI_MPAL_LAN1` by region, selected in `lib/bar-decomp/src/sched.c:74-97` (`uvSetVideoMode`). The
LAN1 control word **includes `VI_CTRL_DIVOT_ON` (0x10)** —
`lib/bar-decomp/tools/ultralib/src/vimodes/vimodentsclan1.c:19` (and pal/mpal equivalents);
`VI_CTRL_DIVOT_ON 0x00010` at `.../include/PR/rcp.h:560`. There is **no dynamic divot toggle**: the
only `osViSetSpecialFeatures` calls touch dither/gamma (`sched.c:236`, `uvgfxmgr_rom.c:517-520`),
never `OS_VI_DIVOT_ON/OFF`. The runtime's dummy VI seed also sets it
(`lib/N64ModernRuntime/ultramodern/src/events.cpp:431`). So `present.screenVI.status.divotEnable` is
**always 1** for BAR.
- **Decision:** default the setting to **Auto** (= follow the bit = always on for BAR). The meaningful
  user override is therefore **"Off"** (escape hatch if the median harms detail); "On" is redundant
  with Auto here but kept for generality.
- **Consequence for B1:** because the bit is frame-global (as on hardware), the divot median runs over
  the **whole composited frame, HUD included** — which is exactly why B1 needs the edge-gating +
  horizontal-only + toggle (a coverage-aware version is B3).

### B1. The shader median (core)
**[lib/rt64/src/shared/rt64_video_interface.h](../lib/rt64/src/shared/rt64_video_interface.h)** — add
two fields to the CB. ✅ The push-constant layout is registered as
`addPushConstant(0, 0, sizeof(interop::VideoInterfaceCB), ...)`
([rt64_shader_library.cpp:594,609](../lib/rt64/src/render/rt64_shader_library.cpp)) — it's
`sizeof`-based, so growing the struct needs **no layout-size edit**. (Keep it push-constant friendly;
pad to 16B only if a backend complains.)
```cpp
struct VideoInterfaceCB {
    float2 videoResolution;
    float2 textureResolution;
    float gamma;
    uint  divotFilter;     // 0 = off, 1 = on
    float divotThreshold;  // outlier gate (linear 0..1), e.g. 0.06
    // float _pad;         // add if the target backend requires 16B alignment
};
```

**[lib/rt64/src/shaders/VideoInterfacePS.hlsl](../lib/rt64/src/shaders/VideoInterfacePS.hlsl)** — add a
divot wrapper and route the non-AA path through it:
```hlsl
// N64 VI "divot": per-channel horizontal median-of-3 at native input-texel spacing. Fills the 1px
// see-through holes the RDP leaves where coplanar/abutting polygons meet (hardware fills these at
// scanout). Gated so it only swaps a center texel that is an outlier vs BOTH horizontal neighbors
// while those neighbors agree — preserving genuine 1px detail as much as is possible without coverage.
float4 SampleInputDivot(float2 uv) {
    float4 c = SampleInput(uv);
    if (gConstants.divotFilter == 0) {
        return c;
    }
    const float2 dx = float2(1.0f / gConstants.textureResolution.x, 0.0f);
    float3 l = SampleInput(uv - dx).rgb;
    float3 r = SampleInput(uv + dx).rgb;
    float3 med = max(min(l, r), min(max(l, r), c.rgb));     // per-channel median of {l,c,r}
    float3 neighborsAgree = step(abs(l - r), gConstants.divotThreshold);
    float3 centerIsOutlier = step(gConstants.divotThreshold, abs(c.rgb - med));
    float3 useMedian = neighborsAgree * centerIsOutlier;
    return float4(lerp(c.rgb, med, useMedian), c.a);
}
```
Then in `PSMain`, replace `SampleInput(...)` with `SampleInputDivot(...)` in the **non-AA** branch
(lines 38-39) **and** route the `PixelAntialiasing` return (lines 27-33) through `SampleInputDivot`
too, so the divot is filtering-independent. ✅ Confirmed: this one source file compiles to two blobs —
**Regular** ([CMakeLists.txt:546](../lib/rt64/CMakeLists.txt)) and **Pixel** (`-D PIXEL_ANTIALIASING`,
[:547](../lib/rt64/CMakeLists.txt)); `videoInterfaceNearest` **and** `videoInterfaceLinear` (our
runtime path) both use the Regular blob
([rt64_shader_library.cpp:601,615-616](../lib/rt64/src/render/rt64_shader_library.cpp)), and
`videoInterfacePixel` uses the Pixel blob ([:618-620](../lib/rt64/src/render/rt64_shader_library.cpp)).
Editing `VideoInterfacePS.hlsl` recompiles both. (RT64's `UserConfiguration::filtering` default is set
in the `UserConfiguration()` ctor — a 1-line confirm at impl time — but covering both branches makes it
moot.)

**[lib/rt64/src/render/rt64_vi_renderer.cpp:76-79](../lib/rt64/src/render/rt64_vi_renderer.cpp)** — set
the new CB fields from `RenderParams`:
```cpp
pushConstants.divotFilter    = p.divotFilter ? 1u : 0u;
pushConstants.divotThreshold = p.divotThreshold;
```

**[lib/rt64/src/render/rt64_vi_renderer.h:18-32](../lib/rt64/src/render/rt64_vi_renderer.h)** — add to
`RenderParams`:
```cpp
bool  divotFilter = false;
float divotThreshold = 0.06f;
```

**[lib/rt64/src/hle/rt64_present_queue.cpp:314-340](../lib/rt64/src/hle/rt64_present_queue.cpp)** — set
`renderParams.divotFilter` where the other per-present fields are set (alongside `renderParams.filtering`
at line 323). `present.screenVI` is already `renderParams.vi` (line 324), so the game bit is in hand:
```cpp
renderParams.divotFilter = resolveDivot(divotMode, present.screenVI.status.divotEnable);
// resolveDivot: On => true; Off => false; Auto => (game divotEnable bit)
```

> **Resolution behavior (why this is correct):** the divot samples at `1/textureResolution`, and
> `textureResolution` is the **native** fb size (the resolved/downsampled texture, §2). So neighbors
> are native pixels at any window size, and with SSAA the VI already sees a native, supersampled image
> — divot + SSAA compose. (At `res_option = 2×/Auto` the fb is rendered _above_ native, so its holes
> are wider than 1 texel and a 3-tap median fills them less completely — expected; native res, our
> default, is the divot's best case.)

### B2. The Settings toggle (Auto / On / Off)
A per-frame shader flag, so — unlike MSAA/SSAA — it **applies live** (not subject to the `m_live_*`
restart lock).

- **[ultramodern config.hpp](../lib/N64ModernRuntime/ultramodern/include/ultramodern/config.hpp)** —
  add an enum mirroring `HighPrecisionFramebuffer` (lines 54-59) and a field on `GraphicsConfig`
  (after line 73), plus a `NLOHMANN_JSON_SERIALIZE_ENUM` table (after line 130):
  ```cpp
  enum class DivotFilter { Auto, On, Off, OptionCount };
  // ... in GraphicsConfig:
  DivotFilter divot_option;
  // ... serialize table:
  NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::DivotFilter, {
      {ultramodern::renderer::DivotFilter::Auto, "Auto"},
      {ultramodern::renderer::DivotFilter::On,   "On"},
      {ultramodern::renderer::DivotFilter::Off,  "Off"},
  });
  ```
- **[src/game/config.cpp](../src/game/config.cpp)** — persist it (mirror `hpfb_option`):
  `graphics_to_json` (after line 77) `{"divot_option", c.divot_option},`; `graphics_from_json`
  (after line 95) `c.divot_option = j.value("divot_option", c.divot_option);`;
  `default_graphics_config` (after line 131) `c.divot_option = DivotFilter::Auto;` (or `On` per B0).
- **[src/ui/bar_ui.cpp](../src/ui/bar_ui.cpp)** — add to `config_to_controls` (after line 152) and
  `controls_to_config_and_apply` (after line 171), exactly like `hpfb_option`:
  ```cpp
  set_control_value("divot_option", enum_to_str(g_working_config.divot_option));
  // ...
  g_working_config.divot_option = str_to_enum(control_value("divot_option", enum_to_str(g_working_config.divot_option)), g_working_config.divot_option);
  ```
- **[src/ui/assets/config.rml](../src/ui/assets/config.rml)** — add a row (note: **no** "Applies on
  restart" — it's live):
  ```html
  <div class="row">
      <span class="label">Seam filter (VI divot)<span class="sub">Fills 1px cracks between surfaces</span></span>
      <select id="divot_option">
          <option value="Auto">Auto (match game)</option>
          <option value="On">On</option>
          <option value="Off">Off</option>
      </select>
  </div>
  ```
- **RT64 `UserConfiguration`** ✅ channel confirmed — mirror `filtering` exactly (it's the closest
  analog: a per-present, live-applied render option). The verified path is:
  `userConfig.filtering` → `ext.sharedResources->userConfig.filtering`
  ([rt64_present_queue.h:112](../lib/rt64/src/hle/rt64_present_queue.h), copied into the present-queue
  member alongside `removeBlackBorders`/`refreshRate`) → `renderParams.filtering = filtering;`
  ([rt64_present_queue.cpp:323](../lib/rt64/src/hle/rt64_present_queue.cpp)). So:
  1. Add `DivotFilter divotFilter` (+ a `DivotFilter {Auto,On,Off}` enum) to `UserConfiguration`
     ([rt64_user_configuration.h:44-104](../lib/rt64/src/common/rt64_user_configuration.h), next to
     `Filtering`/`filtering`); set its default in the `UserConfiguration()` ctor and add a
     `NLOHMANN_JSON_SERIALIZE_ENUM` table like the others.
  2. In the present queue, add a member and `divotMode = ext.sharedResources->userConfig.divotFilter;`
     (next to `filtering` at .h:112), then
     `renderParams.divotFilter = resolveDivot(divotMode, present.screenVI.status.divotEnable);` (next to
     `.filtering = filtering` at .cpp:323). `present.screenVI` is already in scope (`renderParams.vi`).
- **[src/main/rt64_render_context.cpp](../src/main/rt64_render_context.cpp)** — add a
  `to_rt64(DivotFilter)` helper (mirroring `to_rt64(Antialiasing)` at lines 52-60) and
  `app->userConfig.divotFilter = to_rt64(config.divot_option);` in `set_application_user_config`
  (lines 78-117). ✅ Because it isn't MSAA/resolution, the `m_live_*` re-assertion in `update_config`
  (lines 306-309) leaves it alone → it updates **live** via `updateUserConfig(true)`, exactly like
  `refreshRate`/`filtering`.

### B3. (Future, larger — not now) Coverage-gated divot
The honest limitation of B1: without per-pixel coverage, a median can't tell a 1px _hole_ from an
intentional 1px _line_ — both are outliers. Mitigations in B1: horizontal-only (preserves vertical 1px
HUD lines), threshold gate, neighbors-agree gate, and the Auto/Off toggle. The fully faithful version
needs RT64's rasterizer to emit a coverage/edge mask and pass it to the VI stage so the divot fires
only on real silhouette edges (what hardware does). That's a multi-file RT64 change (raster PS output
+ aux render target + plumbing) — list it as a follow-up only if B1's median visibly harms HUD/1px
detail in testing.

### B — testing
With the divot **On**, navigate into a track and inspect a known seam: it should close. Then scan the
HUD / thin geometry (lap counter, rail edges) for any 1px detail the median eats; if present, raise
`divotThreshold`, keep it horizontal-only, or fall back to Auto. Toggle On/Off live in the menu to A/B
the same frame. Confirm no regression at `res_option = 2×` (seam reduced but maybe not gone — expected).

---

## 5. Settings-menu summary (new/changed controls)

| Control | id | Type | Values | Applies | Fix |
|---|---|---|---|---|---|
| Anti-aliasing (exists; new default) | `msaa_option` | select | None / 2× / 4× / 8× — **default 4×** | restart | A1 |
| Supersampling (SSAA) (new) | `ds_option` | select | Off(1) / 2× / 4× | restart | A2 |
| Seam filter (VI divot) (new) | `divot_option` | select | Auto / On / Off | **live** | B2 |

All three sit in the existing Graphics `<form id="graphics_form">`; the form's delegated `change`
handler ([bar_ui.cpp:308-310](../src/ui/bar_ui.cpp)) already re-reads + applies + saves on any change,
so no new event wiring is needed beyond the get/set lines above.

---

## 6. Sequencing & risk

1. **A1 + A2** first — three edits in our own tree (config default + RML + two get/set lines), no RT64
   changes, immediate visible softening. Lowest risk; unblocked today.
2. ~~**B0** — confirm the divot bit.~~ ✅ Done (static): divot is unconditionally on → default Auto.
3. ✅ **FORK `lib/rt64` — DONE 2026-06-30 (superproject commit `2d8383a`).** Forked `rt64/rt64` →
   `bryankruman/rt64` and repointed the submodule (`origin` = fork, `upstream` = `rt64/rt64`, pinned
   `f0728a2` unchanged) — mirroring `16a9e2c`. B's RT64-side changes can now be committed: edit in
   `lib/rt64`, commit, `git push origin HEAD:main`, then bump the gitlink in the superproject.
4. **B1** — RT64 shader + CB + RenderParams + present-queue set. Self-contained; folds into an existing
   shader; runs at native res. Medium risk (touches RT64's VI path, but additively and behind a flag).
5. **B2** — config field + menu control + the confirmed plumbing hops (§4). Live-toggleable, so easy to
   A/B and to disable if it harms detail.
6. **B3** — only if B1 visibly harms 1px detail.

**Touch list:** A = `src/game/config.cpp`, `src/ui/assets/config.rml`, `src/ui/bar_ui.cpp`.
B (rt64 fork) = `lib/rt64/src/shared/rt64_video_interface.h`, `lib/rt64/src/shaders/VideoInterfacePS.hlsl`,
`lib/rt64/src/render/rt64_vi_renderer.{h,cpp}`, `lib/rt64/src/hle/rt64_present_queue.{h,cpp}`,
`lib/rt64/src/common/rt64_user_configuration.{h,cpp}`. B (our forks/tree) =
`lib/N64ModernRuntime/ultramodern/include/ultramodern/config.hpp`, `src/game/config.cpp`,
`src/ui/assets/config.rml`, `src/ui/bar_ui.cpp`, `src/main/rt64_render_context.cpp`.

> **Submodule reality (2026-06-30):** `lib/rt64` → `bryankruman/rt64` **(ours; forked + repointed, commit
> `2d8383a`; `upstream` = `rt64/rt64`)**, `main` @ `f0728a2`. `lib/N64ModernRuntime` →
> `bryankruman/N64ModernRuntime` (ours, has `upstream`),
> currently `main` @ `5d7b382` — note the memory says runtime work lives on a `bar` branch, but the
> submodule is on `main`; confirm the intended branch before committing B2's ultramodern edit.
> `lib/bar-decomp` → `bryankruman/BeetleDecomp` (ours).

## 7. Open questions / checkpoints
Resolved by static verification (2026-06-30):
- ✅ **B0 — divot bit:** BAR's LAN1 mode sets `VI_CTRL_DIVOT_ON`, no dynamic toggle → default **Auto**.
- ✅ **B2 plumbing:** confirmed channel `userConfig.filtering` → `sharedResources->userConfig`
  (present_queue.h:112) → `renderParams` (present_queue.cpp:323); mirror with `divotFilter`.
- ✅ **Shader build:** `VideoInterfacePS.hlsl` → Regular+Pixel blobs; Nearest+Linear use Regular (our
  path). Editing the one file covers it; route both branches to be filtering-independent.
- ✅ **Push-constant size:** layout uses `sizeof(VideoInterfaceCB)` → growing the struct needs no
  layout edit.

Remaining (need a build or the running game — deferred):
- ✅ **Forked `lib/rt64`** → `bryankruman/rt64` + repointed (2026-06-30, commit `2d8383a`); B's RT64
  edits can now be committed on the fork.
- **CB alignment:** build-time confirm push-constant offset rules on D3D12 + Vulkan after adding the two
  fields (pad to 16B only if a backend complains). Needs a build.
- **Detail preservation (runtime):** does the horizontal median nibble any 1px HUD/world detail? Tune
  `divotThreshold` / keep horizontal-only / expose the threshold if necessary. Needs the game.
- **RenderDoc capture (runtime):** one capture of a seam to reconfirm it's the coverage/edge class
  (background through the crack), not a decal/Z-fight case (out of scope). Needs the game.
- **ultramodern branch:** confirm whether B2's `config.hpp` edit should land on `main` or a `bar` branch
  of our N64ModernRuntime fork (submodule is currently on `main`).
