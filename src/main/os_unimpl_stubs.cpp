// os_unimpl_stubs.cpp — native no-op stubs for low-level libultra OS functions.
//
// N64Recomp lists these in `ignored_funcs` (so it does NOT recompile them), and the runtime
// (librecomp/ultramodern) does NOT provide them either — normally a game never reaches them
// because the runtime reimplements the higher-level callers. BAR's recompiled code DOES call
// them, so we provide empty stubs here so the port links. This mirrors how librecomp stubs
// similar low-level access functions (e.g. __osPiGetAccess_recomp is an empty body).
//
// !!! NO-OPs — sufficient to LINK, NOT correct for running. These touch hardware/threading the
// runtime models differently:
//   __osEnqueueThread / __osPopThread        N64 thread queue (ultramodern uses native threads)
//   __osViSwapContext                        VI framebuffer swap (RT64 owns presentation)
//   __osSiGetAccess/RelAccess/RawStartDma    controller (SI) bus + DMA
//   osPiReadIo / __osPiRawReadIo             cartridge (PI) IO
//   __osTimerInterrupt                       timer/scheduler tick
// TODO(BAR): during runtime bring-up, give these real behavior (or reimplement the higher-level
// callers so these are never reached). See docs/recomp notes.
#include "recomp.h"
#include <ultramodern/ultramodern.hpp>
#include <cstdlib>
#include <chrono>   // R6 diagnostic: BAR_DBG_FPS loop-rate counter (remove after)
#include <cstdio>   // R6 diagnostic

#include "main/bar_cheats.h"   // bar_cheats::apply_frame (host-side RDRAM cheat pokes)

// Keyboard -> N64 pad mapping lives in main.cpp (it owns the Win32 window / focus).
extern "C" uint16_t bar_poll_keyboard(int port, int8_t* stick_x, int8_t* stick_y);

// R6 diagnostic (env-gated BAR_DBG_SLIDE): trace the menu page-transition slide. Called from the
// recompiled transition-start (func_selection_00402E98) and slide-draw (func_selection_00418800) to
// see, per navigation, whether the slide draws over many frames or just 1-2 (instant). Remove after R6.
extern "C" void bar_dbg_slide(const char* tag) {
    static const bool on = std::getenv("BAR_DBG_SLIDE") != nullptr;
    if (!on) return;
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count();
    static long long t0 = 0; if (t0 == 0) t0 = ms;
    static unsigned long n = 0;
    std::fprintf(stderr, "[BAR_DBG_SLIDE] %-9s #%lu t+%lldms\n", tag, ++n, ms - t0);
}
extern "C" void bar_dbg_caller(const char* tag, unsigned addr) {
    static const bool on = std::getenv("BAR_DBG_SLIDE") != nullptr;
    if (!on) return;
    std::fprintf(stderr, "[BAR_DBG_SLIDE] %s caller-ra=0x%08X\n", tag, addr);
}

#define BAR_OS_STUB(name) \
    extern "C" void name(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; }

BAR_OS_STUB(__osViSwapContext_recomp)
BAR_OS_STUB(__osTimerInterrupt_recomp)
BAR_OS_STUB(__osSiGetAccess_recomp)
BAR_OS_STUB(__osSiRelAccess_recomp)

// __osSiRawStartDma: BAR drives the controller (SI/PIF) bus directly through the low-level libultra
// path — osContStartReadData() does __osPackReadData() (which fills the PIF RAM with button=0xFFFF
// placeholders) -> __osSiRawStartDma(OS_WRITE) -> osRecvMesg -> __osSiRawStartDma(OS_READ) -> parse.
// ultramodern stubs this (it expects games to use the reimplemented high-level osContStartReadData),
// so two things must happen here or the game (a) deadlocks waiting for the SI-complete event and
// (b) reads the 0xFFFF placeholders as "every button held":
//   1. Post the SI event so the game's osRecvMesg(SI queue) returns (like send_si_message in the
//      high-level path).
//   2. On the READ DMA, write a real controller-read response into the PIF RAM so the game gets
//      actual (currently neutral) input instead of the 0xFFFF placeholder.
// PIF RAM layout per controller is __OSContReadFormat (8 bytes): dummy,txsize,rxsize@2,cmd,
// button@4(BE u16),stick_x@6,stick_y@7. rxsize top bits = CHNL error (0x04 = 4 bytes, no error).
extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int32_t direction = (int32_t)ctx->r4;   // OS_READ = 0, OS_WRITE = 1
    const uint32_t pifram = (uint32_t)ctx->r5;     // &__osContPifRam.ramarray
    // __osMaxControllers (byte @ 0x80032231) is left at 0 by BAR's recompiled controller init, so
    // __osPackReadData (func_8000E6E0) packs ZERO read-button commands — the PIF is just CONT_CMD_END
    // and every controller read returns an empty buffer (no input reaches the game). Force it to
    // MAXCONTROLLERS(4) here (runs during osContInit's SI ops, before the first read-button pack) so a
    // real CONT_CMD_READ_BUTTON gets packed and the joybus read carries actual buttons.
    MEM_B(0, (int64_t)(int32_t)0x80032231) = (int8_t)4;
    // BAR sometimes calls __osSiRawStartDma with a corrupt a1 (observed pifram=0x????21F0 — low bits of
    // the real PIF RAM but a garbage high half) -> MEM_BU faults reading it. Only touch the buffer when
    // a1 is a sane RDRAM (KSEG0, 8 MiB) address; otherwise just post the SI event so we never AV.
    const bool valid_pifram = ((pifram - 0x80000000u) < 0x00800000u);
    // Drive the state machine for testing: BAR_FORCESTATE="frame:state frame:state ..." writes
    // gGameSettings->gameStateFlag (@0x08) once at each listed SI-frame; the game's main loop then
    // calls uvSetGameState(state). Lets us reach later screens / a race without the (unread) buttons.
    { static bool inited = false; static const char* fs = nullptr; static unsigned long long fc = 0;
      if (!inited) { inited = true; fs = std::getenv("BAR_FORCESTATE"); }
      if (fs && *fs) { ++fc; const int64_t GS = (int64_t)(int32_t)0x80025CF0;
        for (const char* p = fs; *p; ) {
            while (*p == ' ') ++p; if (!*p) break;
            char* e = nullptr; long fr = std::strtol(p, &e, 10); long st = 0;
            if (e && *e == ':') st = std::strtol(e + 1, &e, 10);
            p = (e && e != p) ? e : p + 1;
            if ((long)fc == fr) { MEM_W(0X8, GS) = (int32_t)st; }
        } } }
    // Cheats menu: poke the game's RDRAM for any enabled BAR cheats. This hook fires ~once per
    // controller poll (per frame) in both the menus and a race, the right cadence for the
    // "every frame" cheat writes (unlocks, debug-options flags). No-op when nothing is enabled.
    bar_cheats::apply_frame(rdram);
    // R6 diagnostic (env-gated BAR_DBG_FPS): the menu/game main loop polls the controller once per
    // iteration, so this hook's call rate == the loop rate. The page-slide animation advances per loop
    // iteration; if this is >> native 60 Hz the slide completes in ~1 display frame ("disabled"-looking).
    { static const bool dbg = std::getenv("BAR_DBG_FPS") != nullptr;
      if (dbg) {
          static unsigned long n = 0; static long long t0 = 0;
          long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
          ++n; if (t0 == 0) t0 = ms;
          if (ms - t0 >= 1000) { std::fprintf(stderr, "[BAR_DBG_FPS] %lu SI-polls/sec (== menu loop rate)\n", n); n = 0; t0 = ms; }
      } }
    // R6 tooling (env-gated BAR_DBG_STATE): log currentGameState (gGameSettings+0xA4) transitions so a
    // BAR_AUTOPLAY script can be tuned/verified headlessly (boot -> logos -> intro -> menu=0xE -> race=2).
    { static const bool dbg = std::getenv("BAR_DBG_STATE") != nullptr;
      if (dbg) {
          static int32_t last = -0x7FFF;
          int32_t st = (int32_t)MEM_W(0XA4, (int64_t)(int32_t)0x80025CF0);
          if (st != last) {
              long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count();
              std::fprintf(stderr, "[BAR_DBG_STATE] currentGameState -> %d (0x%X) @%lldms\n", st, st, ms);
          }
          last = st;
      } }
    if (direction == 0 /* OS_READ */ && valid_pifram) {
        // The PIF RAM holds one joybus command block per controller (8 bytes for the status-query
        // and button-read commands BAR uses for the menu). Branch on the command byte (offset 3) so
        // we write the matching response; only port 0 is connected. Leave other commands (Controller
        // Pak / EEPROM) untouched so we don't corrupt their (differently sized) buffers.
        // MEM_* expect a SIGN-EXTENDED N64 address (the macros subtract 0xFFFFFFFF80000000): a plain
        // uint32_t 0x800321F0 zero-extends and underflows that subtraction to a +4 GiB out-of-bounds
        // offset -> access violation. Use a sign-extended gpr-style value, exactly like ctx->rN.
        const int64_t pifram_se = (int32_t)ctx->r5;
        for (int i = 0; i < 4; i++) {
            const int64_t blk = pifram_se + (int64_t)(i * 8);
            const unsigned cmd = (unsigned)MEM_BU(3, blk);   // CONT_CMD_* the game packed
            if (cmd != 0 && cmd != 1) continue;              // not status/button -> leave as-is
            if (i != 0) {                                    // ports 1-3: no controller connected
                MEM_B(2, blk) = (int8_t)0x80;                // rxsize: CHNL NO_RESPONSE error
                continue;
            }
            if (cmd == 0) {                  // CONT_CMD_REQUEST_STATUS (controller detect)
                MEM_B(2, blk) = 0x03;        // rxsize=3, no channel error
                MEM_B(4, blk) = 0x05;        // typeh  -> type = typel<<8|typeh = 0x0005 (CONT_TYPE_NORMAL)
                MEM_B(5, blk) = 0x00;        // typel
                MEM_B(6, blk) = 0x00;        // status: no Controller Pak
                MEM_B(7, blk) = 0x00;
            } else {                         // CONT_CMD_READ_BUTTON
                int8_t sx = 0, sy = 0;
                const uint16_t button = bar_poll_keyboard(0, &sx, &sy);
                MEM_B(2, blk) = 0x04;        // rxsize=4, no channel error
                MEM_B(4, blk) = (int8_t)(uint8_t)(button >> 8);
                MEM_B(5, blk) = (int8_t)(uint8_t)(button & 0xFF);
                MEM_B(6, blk) = sx;
                MEM_B(7, blk) = sy;
            }
        }
    }
    ultramodern::send_si_message();
}

BAR_OS_STUB(osPiReadIo_recomp)
BAR_OS_STUB(__osPiRawReadIo_recomp)
BAR_OS_STUB(__osPopThread_recomp)
BAR_OS_STUB(__osEnqueueThread_recomp)
