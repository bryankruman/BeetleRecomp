# Headless testing: scripted input + internal-render screenshots

Everything below is **env-var driven and headless** — no window focus, no window-manager capture, no
manual clicking. Compose the vars on one launch. All are zero-cost when unset.

## 1. Hide the UI overlay — `BAR_SKIP_LAUNCHER=1`
Starts with the RmlUi launcher/overlay hidden (`src/ui/bar_ui.cpp` — sets `g_menu_open=false`, so
`draw_hook` draws nothing). The game renders directly. Use this for every headless test.

## 2. Scripted input — `BAR_AUTOPLAY="frames:hexbtn frames:hexbtn ..."`
Implemented in `src/main/main.cpp` (`bar_autoplay_poll`). Space-separated `frames:hexbuttons` phases,
played in order then held neutral. Bypasses the focus gate; `fc` counts input-poll frames (~60/s once
the game is polling). Logs each phase (`AUTOPLAY -> phase ...`).

N64 button mask (hex): `START=1000  A=8000  B=4000  Z=2000`; d-pad `U=0800 D=0400 L=0200 R=0100`
(d-pad bits also drive the analog stick). Keyboard equivalents: A=`X` B=`Z` Start=`Enter` d-pad=`T/F/G/H`.

### Boot sequence → main menu (verified)
The game boots to a **Controller Pak prompt** (`currentGameState` = 14/0xE and it STAYS 14 through the
prompts AND the menu — the state does not distinguish them, so use screenshots to verify, not the state).
Two prompts, then the menu:
1. "No Controller Pak present … **Start Game Without Saving**" (highlighted) → press **A**.
2. "You will be unable to save … Continue? **Yes / No**" (**No** highlighted by default) → press **Up** to
   move to Yes, then **A**.

Recipe that reaches the main menu:
```
BAR_SKIP_LAUNCHER=1 BAR_AUTOPLAY="120:0 30:8000 150:0 30:0800 150:0 30:8000 700:0"
#   wait ~2s, A(start), wait, Up(→Yes), wait, A(confirm), hold
```
Main-menu **screen transitions** are driven by **A** (select) — each fires the recompiled
`func_selection_00402E98` (the `E98start` marker in `BAR_DBG_SLIDE` traces).

## 3. Internal-render screenshot (RT64) — `RT64_SHOT_TRIGGER=<file> RT64_SHOT_OUT=<png>`
Captures RT64's **actual presented image** via a GPU readback of the swapchain (no window manager). Built
into the RT64 fork (`lib/rt64/src/hle/rt64_present_queue.cpp`, at the draw-hook site): on each present, if
the trigger file exists it copies the swapchain texture → readback buffer → `stbi_write_png` → deletes the
trigger. (Also fixed a plume bug: `D3D12CommandList::copyTextureRegion` crashed on a buffer destination
because it called `setSamplePositions(dstLocation.texture)` with a null texture — `plume_d3d12.cpp`.)

Usage from a shell:
```
touch "$RT64_SHOT_TRIGGER"   # request a capture
sleep 2.5                    # let a present happen
# then read $RT64_SHOT_OUT (a PNG, 960x720 = RT64's internal render)
```

## 4. Diagnostics
- `BAR_DBG_STATE=1` — logs `currentGameState` (gGameSettings 0x80025CF0 + 0xA4) transitions.
- `BAR_DBG_SLIDE=1` — logs the R6 film-roll/selection trace (`E98start`, `func_filmroll_*`).
- `BAR_DBG_FPS=1` — logs the SI-poll (game-loop) rate.
- `BAR_DEBUG_OVERLAYS=1` — logs every module load by 4-char tag (`overlay 'frol' ...`).

## Full example (reach menu + screenshot it)
```
cd build-cmake
RT64_SHOT_TRIGGER=shot_req RT64_SHOT_OUT=shot.png BAR_SKIP_LAUNCHER=1 \
  BAR_AUTOPLAY="120:0 30:8000 150:0 30:0800 150:0 30:8000 700:0" ./BeetleRecomp.exe &
sleep 26; touch shot_req; sleep 2.5   # -> shot.png is the main menu
```
