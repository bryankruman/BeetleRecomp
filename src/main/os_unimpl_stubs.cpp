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

// Keyboard -> N64 pad mapping lives in main.cpp (it owns the Win32 window / focus).
extern "C" uint16_t bar_poll_keyboard(int port, int8_t* stick_x, int8_t* stick_y);

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
    if (direction == 0 /* OS_READ */) {
        // The PIF RAM holds one joybus command block per controller (8 bytes for the status-query
        // and button-read commands BAR uses for the menu). Branch on the command byte (offset 3) so
        // we write the matching response; only port 0 is connected. Leave other commands (Controller
        // Pak / EEPROM) untouched so we don't corrupt their (differently sized) buffers.
        for (int i = 0; i < 4; i++) {
            const uint32_t blk = pifram + (uint32_t)(i * 8);
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
