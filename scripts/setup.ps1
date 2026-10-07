# Bootstrap BeetleRecomp on Windows: fetch deps and build the recompiler tools.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

# Use only the dependency registrations and pins committed in this repository.
Write-Host ">> Updating submodules recursively..."
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "Submodule update failed" }
python scripts/apply-dependency-patches.py
if ($LASTEXITCODE -ne 0) { throw "Reviewed dependency patches failed" }

Write-Host ">> Building N64Recomp + RSPRecomp (Release)..."
cmake -S lib/N64Recomp -B lib/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "Recompiler configuration failed" }
$jobs = if ($env:BEETLE_BUILD_JOBS) { [int]$env:BEETLE_BUILD_JOBS } else { 4 }
if ($jobs -lt 1) { throw "BEETLE_BUILD_JOBS must be positive" }
cmake --build lib/N64Recomp/build --config Release --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "Recompiler build failed" }

Write-Host ">> Copying recompiler executables to repo root..."
Get-ChildItem -Path lib/N64Recomp/build -Recurse -Include N64Recomp.exe,RSPRecomp.exe |
  ForEach-Object { Copy-Item -Force $_.FullName . }

Write-Host ">> Done. Next: scripts/fetch-elf.ps1, then .\N64Recomp.exe BeetleRecomp.toml"
