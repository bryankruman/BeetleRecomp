#!/usr/bin/env bash
# Copy the BAR decomp's symbol-rich ELF into elf/.
# From Windows Git Bash it streams the file out of WSL; inside WSL/Linux it copies directly.
set -euo pipefail
export MSYS_NO_PATHCONV=1   # stop Git Bash mangling the WSL /home/... paths
cd "$(dirname "$0")/.."

DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"
DECOMP_DIR="${DECOMP_DIR:-/home/brysl/projects/bar-decomp}"
ELF_REL="${ELF_REL:-build/beetleadventurerac.us.elf}"

mkdir -p elf
DEST="elf/$(basename "$ELF_REL")"

if command -v wsl.exe >/dev/null 2>&1 && [ ! -d "$DECOMP_DIR" ]; then
  echo ">> Streaming $DECOMP_DIR/$ELF_REL out of WSL ($DISTRO) -> $DEST"
  if ! wsl.exe -d "$DISTRO" -- test -f "$DECOMP_DIR/$ELF_REL"; then
    echo "!! ELF not found in WSL. Build the decomp first:"
    echo "     (WSL) cd $DECOMP_DIR && source .venv/bin/activate && make -j6"
    exit 1
  fi
  wsl.exe -d "$DISTRO" -- cat "$DECOMP_DIR/$ELF_REL" > "$DEST"
else
  echo ">> Copying $DECOMP_DIR/$ELF_REL -> $DEST"
  cp "$DECOMP_DIR/$ELF_REL" "$DEST"
fi

echo ">> ELF in place: $DEST ($(wc -c < "$DEST") bytes)"
