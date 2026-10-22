#!/usr/bin/env bash
# Bootstrap BeetleRecomp: fetch dependencies and build the recompiler tools.
# Run once after cloning; safe to re-run.
set -euo pipefail
cd "$(dirname "$0")/.."

# The committed .gitmodules and Git index are the source of dependency pins.
# Do not run `submodule add` on an already registered, uninitialized checkout.
echo ">> Updating submodules recursively..."
git submodule update --init --recursive

python3 scripts/apply-dependency-patches.py

echo ">> Building N64Recomp + RSPRecomp (Release)..."
cmake -S lib/N64Recomp -B lib/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build lib/N64Recomp/build --config Release --parallel "${BEETLE_BUILD_JOBS:-4}"

echo ">> Copying recompiler executables to repo root..."
find lib/N64Recomp/build -maxdepth 2 -type f \
  \( -name 'N64Recomp' -o -name 'N64Recomp.exe' -o -name 'RSPRecomp' -o -name 'RSPRecomp.exe' \) \
  -exec cp -f {} . \;

echo ">> Done."
echo ">> Next: scripts/fetch-elf.sh, then resolve private original bindings as shown in BUILDING.md before N64Recomp."
