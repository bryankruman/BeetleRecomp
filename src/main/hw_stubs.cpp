// hw_stubs.cpp — native stubs for libultra functions that poke RCP hardware registers directly.
//
// A handful of low-level libultra functions were recompiled raw (the decomp didn't symbol-match
// them, so they aren't in BeetleRecomp.toml `ignored`/`stubs`). They MEM_W/MEM_B KSEG1 register
// addresses (0xA4xxxxxx — AI/PI/SP), which aren't memory-mapped in the recomp and fault out of
// bounds. scripts/fix-recompiled.sh renames each generated definition to <name>__hwstub_orig so the
// wrapper below owns the symbol all callers bind to.
//
// Proper long-term fix: add these to BeetleRecomp.toml `stubs` (or instruction-patch the offending
// hardware accesses) and regenerate with N64Recomp. Stubbed here for bring-up.
#include "recomp.h"

// func_8000E460: osAiGetLength() — reads AI_LEN_REG (0xA4500004), the remaining audio DMA length.
// Audio output isn't wired yet; report the DMA engine as idle (0).
extern "C" void func_8000E460(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}
