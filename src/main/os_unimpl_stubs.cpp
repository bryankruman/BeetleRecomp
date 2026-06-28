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

#define BAR_OS_STUB(name) \
    extern "C" void name(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; }

BAR_OS_STUB(__osViSwapContext_recomp)
BAR_OS_STUB(__osTimerInterrupt_recomp)
BAR_OS_STUB(__osSiGetAccess_recomp)
BAR_OS_STUB(__osSiRelAccess_recomp)
BAR_OS_STUB(__osSiRawStartDma_recomp)
BAR_OS_STUB(osPiReadIo_recomp)
BAR_OS_STUB(__osPiRawReadIo_recomp)
BAR_OS_STUB(__osPopThread_recomp)
BAR_OS_STUB(__osEnqueueThread_recomp)
