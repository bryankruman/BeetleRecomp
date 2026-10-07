#!/usr/bin/env bash
# Build the MIPS patches (patches/*.c -> patches/patches.elf).
#
# WHY THIS WRAPPER EXISTS
#
# Patches are C cross-compiled to MIPS, which needs an LLVM built with the MIPS backend. The
# Windows LLVM installed for the host build (22.1.8, via winget) does NOT have one — `llc --version`
# lists no MIPS target and `-target mips` fails outright with "No available targets are compatible
# with triple mips". That is not a flags problem and no amount of coaxing fixes it.
#
# Ubuntu's packaged clang does carry every target, so the patches are built in WSL exactly as the
# decomp is. clang-18 is used deliberately because 19.x is documented as miscompiling MIPS.
# The package's minor version depends on the distribution; record `clang-18 --version` in build
# evidence. The isolated Ubuntu worker's completed patch build used LLVM 18.1.3.
#
# One-time setup inside WSL:
#     sudo apt install -y clang-18 lld-18
#
# Usage (from Windows Git Bash, or natively on Linux):
#     scripts/build-patches.sh            # build
#     scripts/build-patches.sh clean      # clean
#
# Override the distro or the toolchain if your setup differs:
#     WSL_DISTRO=Ubuntu-24.04 PATCH_CC=clang-18 PATCH_LD=ld.lld-18 scripts/build-patches.sh
set -euo pipefail

DISTRO="${WSL_DISTRO:-Ubuntu-24.04}"
PATCH_CC="${PATCH_CC:-clang-18}"
PATCH_LD="${PATCH_LD:-ld.lld-18}"
TARGET="${1:-}"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

run_make() {
    # $1 = patches directory as seen by the shell that will run make
    local dir="$1"
    echo ">> building patches in $dir with CC=$PATCH_CC LD=$PATCH_LD"
    if ! command -v "$PATCH_CC" >/dev/null 2>&1; then
        echo "!! $PATCH_CC not found. Install the MIPS-capable toolchain first:" >&2
        echo "     sudo apt install -y clang-18 lld-18" >&2
        exit 1
    fi
    make -C "$dir" CC="$PATCH_CC" LD="$PATCH_LD" $TARGET
}

if [ -n "${WSL_DISTRO_NAME:-}" ] || [ "$(uname -s 2>/dev/null)" = "Linux" ]; then
    # Already inside Linux/WSL.
    run_make "$repo_root/patches"
else
    # On Windows: re-enter this script inside WSL, addressing the repo through /mnt/c.
    if ! command -v wsl.exe >/dev/null 2>&1; then
        echo "!! wsl.exe not found, and this host has no MIPS-capable clang." >&2
        exit 1
    fi
    # Translate the repo path into something WSL can see. Git Bash reports MSYS paths (/c/Users/...)
    # while other shells report Windows paths (C:\Users\...); both must become /mnt/c/Users/...
    wsl_root="$(printf '%s' "$repo_root" \
        | sed -E 's|\\|/|g; s|^([A-Za-z]):|/mnt/\L\1|; s|^/([A-Za-z])/|/mnt/\1/|')"
    echo ">> entering WSL ($DISTRO) at $wsl_root"
    MSYS_NO_PATHCONV=1 wsl.exe -d "$DISTRO" -- bash -lc \
        "PATCH_CC='$PATCH_CC' PATCH_LD='$PATCH_LD' bash '$wsl_root/scripts/build-patches.sh' $TARGET"
fi
