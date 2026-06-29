#!/usr/bin/env bash
# Post-process N64Recomp output. Run AFTER `./N64Recomp BeetleRecomp.toml`, BEFORE building.
#
# Works around an N64Recomp codegen quirk: a load into $zero (`lw $zero, ...`) is emitted as
# `0 = MEM_W(...)`, which is not valid C ($zero is the literal 0). Rewrite to `(void)MEM_W(...)`
# so the read still happens (for side effects) but the bogus assignment is dropped.
#
# Second quirk: recomp_overlays.inl emits some relocs with an empty `.type` field
# (`.type =  }`). These are MIPS reloc types beyond N64Recomp's reloc_names table (index > 7,
# e.g. GOT16/CALL16/GPREL32). librecomp's RelocEntryType only covers 0..7 and the recompiled
# code resolves addresses inline via RELOC_HI16/LO16 (the section reloc table is read only by the
# mod system), so these entries are inert for running the base game. Rewrite them to R_MIPS_NONE
# so the table is valid C++.
set -euo pipefail
RF="$(cd "$(dirname "$0")/.." && pwd)/RecompiledFuncs"
[ -d "$RF" ] || { echo "fix-recompiled: $RF not found (run N64Recomp first)"; exit 1; }
before=$( { grep -rhcE '^[[:space:]]*0 = ' "$RF"/*.c 2>/dev/null || true; } | awk '{s+=$1} END{print s+0}')
sed -i -E 's/^([[:space:]]*)0 = /\1(void)/' "$RF"/*.c
echo "fix-recompiled: rewrote $before \$zero-load line(s) to (void)MEM_*(...)"

inl="$RF/recomp_overlays.inl"
if [ -f "$inl" ]; then
    reloc_before=$(grep -c '\.type =[[:space:]]*}' "$inl" || true)
    sed -i 's/\.type =[[:space:]]*}/.type = R_MIPS_NONE }/g' "$inl"
    echo "fix-recompiled: rewrote $reloc_before empty reloc .type field(s) to R_MIPS_NONE"
fi

# Overlay bridge: rename the generated uvDoModuleRelocs DEFINITION so src/main/overlay_bridge.cpp
# can own the `uvDoModuleRelocs` symbol. Its wrapper registers the just-loaded relocatable module
# with librecomp (load_overlay_by_id) so the module's recompiled functions resolve, then calls
# uvDoModuleRelocs_orig. Only the RECOMP_FUNC definition is renamed; all call sites bind to the
# wrapper. Idempotent (no-op once renamed).
if grep -rlq 'RECOMP_FUNC void uvDoModuleRelocs(' "$RF"/*.c 2>/dev/null; then
    sed -i 's/RECOMP_FUNC void uvDoModuleRelocs(/RECOMP_FUNC void uvDoModuleRelocs_orig(/' "$RF"/*.c
    echo "fix-recompiled: renamed uvDoModuleRelocs definition -> uvDoModuleRelocs_orig (overlay bridge)"
fi

# Hardware-register stubs: libultra functions recompiled raw that poke RCP registers (AI/PI/SP),
# which aren't memory-mapped in the recomp. Rename each generated definition so src/main/hw_stubs.cpp
# can own the symbol with a safe stub. Add a name here AND a stub in hw_stubs.cpp together.
for fn in func_8000E460; do
    if grep -rlq "RECOMP_FUNC void ${fn}(" "$RF"/*.c 2>/dev/null; then
        sed -i "s/RECOMP_FUNC void ${fn}(/RECOMP_FUNC void ${fn}__hwstub_orig(/" "$RF"/*.c
        echo "fix-recompiled: stub-renamed ${fn} (raw hardware-register access)"
    fi
done
