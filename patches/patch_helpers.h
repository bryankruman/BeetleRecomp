#ifndef PATCH_HELPERS_H
#define PATCH_HELPERS_H
// Dual-target shim: patch C compiles as MIPS (real game ABI) and, for tooling, can
// also compile natively. Mirrors Zelda64Recompiled/patches/patch_helpers.h.

#ifdef MIPS
#  include "ultra64.h"     // from the decomp headers (DECOMP_INCLUDE)
#else
#  include "recomp.h"      // from the runtime, native side
#endif

#ifdef __cplusplus
#  define EXTERNC extern "C"
#else
#  define EXTERNC
#endif

#ifdef MIPS
#  define DECLARE_FUNC(type, name, ...) EXTERNC type name(__VA_ARGS__)
#else
#  define DECLARE_FUNC(type, name, ...) EXTERNC void name(uint8_t* rdram, recomp_context* ctx)
#endif

#endif // PATCH_HELPERS_H
