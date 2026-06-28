// Example recomp patch. Compiles to MIPS and OVERRIDES the same-named game function
// at link time (patches are linked before the recompiler's output, so they win).
//
// Replace this with real overrides as you find functions that poke hardware the
// runtime should handle instead. The real RECOMP_PATCH / RECOMP_HOOK / RECOMP_EXPORT
// macros come from the runtime headers; the fallbacks below are illustrative so this
// file is self-contained until the decomp/runtime headers are wired in.
#include "patch_helpers.h"

#ifndef RECOMP_PATCH
#  define RECOMP_PATCH  __attribute__((section(".recomp_patch")))
#endif
#ifndef RECOMP_EXPORT
#  define RECOMP_EXPORT __attribute__((section(".recomp_export")))
#endif

// A trivial export so patches.elf has at least one symbol to recompile.
RECOMP_EXPORT int beetlerecomp_patches_present(void) {
    return 1;
}

// Example of the real pattern (commented; needs decomp headers to compile):
// RECOMP_PATCH void SomeGameFunc(void) {
//     // replacement body
// }
