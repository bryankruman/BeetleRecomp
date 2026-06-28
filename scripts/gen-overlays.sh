#!/usr/bin/env bash
# Generate a CANDIDATE relocatable-sections list for N64Recomp from an ELF.
# Run where `readelf` exists (WSL, or Git Bash with binutils). See overlays.us.txt
# for why the matching ELF's sections need review (and why a per-module recomp.elf
# is the better long-term input).
set -euo pipefail
cd "$(dirname "$0")/.."
ELF="${1:-elf/beetleadventurerac.us.elf}"
OUT="${2:-overlays.us.txt}"

command -v readelf >/dev/null || { echo "readelf not found — run in WSL or install binutils."; exit 1; }
[ -f "$ELF" ] || { echo "ELF not found: $ELF (run scripts/fetch-elf.* first)"; exit 1; }

{
  echo "# Auto-generated from $ELF by scripts/gen-overlays.sh"
  echo "# REVIEW: keep only relocatable MODULE sections; drop base/system sections."
  readelf -SW "$ELF" \
    | sed -E 's/^\s*\[\s*[0-9]+\]\s+//' \
    | awk '{print $1}' \
    | grep -E '^\.' \
    | grep -viE '^\.(text|data|rodata|bss|sdata|sbss|symtab|strtab|shstrtab|debug|comment|note|pdr|mdebug|reginfo|MIPS|gnu|rel|got|dynamic|hash)' \
    | sort -u
} > "$OUT"

echo ">> Wrote $(grep -cE '^\.' "$OUT") candidate sections to $OUT (review before use)."
