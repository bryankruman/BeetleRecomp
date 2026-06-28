/* Symbol addresses that patches reference in the original game binary.
 * Populate from the decomp's symbol_addrs, e.g.:
 *
 *     PROVIDE(osSetTime          = 0x800XXXXX);
 *     PROVIDE(uvGfxDisplayList   = 0x800XXXXX);
 *
 * Source: lib/bar-decomp/linker_scripts/us/symbol_addrs.txt (+ libultra_symbols.txt).
 * (A generator that converts those into PROVIDE() lines is a good helper to add.) */
