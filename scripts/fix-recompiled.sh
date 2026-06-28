#!/usr/bin/env bash
# Post-process N64Recomp output. Run AFTER `./N64Recomp BeetleRecomp.toml`, BEFORE building.
#
# Works around an N64Recomp codegen quirk: a load into $zero (`lw $zero, ...`) is emitted as
# `0 = MEM_W(...)`, which is not valid C ($zero is the literal 0). Rewrite to `(void)MEM_W(...)`
# so the read still happens (for side effects) but the bogus assignment is dropped.
set -euo pipefail
RF="$(cd "$(dirname "$0")/.." && pwd)/RecompiledFuncs"
[ -d "$RF" ] || { echo "fix-recompiled: $RF not found (run N64Recomp first)"; exit 1; }
before=$(grep -rhcE '^[[:space:]]*0 = ' "$RF"/*.c 2>/dev/null | awk '{s+=$1} END{print s+0}')
sed -i -E 's/^([[:space:]]*)0 = /\1(void)/' "$RF"/*.c
echo "fix-recompiled: rewrote $before \$zero-load line(s) to (void)MEM_*(...)"
