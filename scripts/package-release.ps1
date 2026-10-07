# Stage a built Windows tree into a release folder and zip it. Adapted from the recomp framework's
# tools/package_release.ps1 (the series' shared packager).
#
#   powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 -Version 0.3.0-alpha
#
# What goes in: the executable, the three DLLs it cannot start without (SDL2, dxcompiler, dxil), the
# assets/ folder the menus draw from, this project's license (COPYING), the third-party notices, the
# README and the license texts of the vendored libraries. The debug symbols (PDB) go into a second zip,
# named -debug-symbols: only a crash report needs them. Nothing here is a ROM, a save or a user config,
# and the script refuses to continue if it finds one where it is staging.

param(
    [string]$BuildDir = "build-frontend",
    [Parameter(Mandatory = $true)][string]$Version,   # e.g. 0.3.0-alpha
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$exeName = "BeetleRecomp"
$exe = Join-Path $BuildDir "$exeName.exe"
if (-not (Test-Path $exe)) {
    throw "no executable at $exe -- build first (see BUILDING.md)"
}

# Release asset naming as in earlier releases: BeetleRecomp-v0.2.0-alpha-windows-x64.zip
$name = "$exeName-v$Version-windows-x64"
$outFull = [IO.Path]::GetFullPath((Join-Path $root $OutDir))
if (-not $outFull.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutDir must be inside the repository"
}
if ($Version -notmatch "^[0-9A-Za-z][0-9A-Za-z._-]*$") { throw "unsafe version string" }
$stage = Join-Path $outFull $name
if (Test-Path -LiteralPath $stage) { throw "staging folder already exists; choose a new version or archive it" }
New-Item -ItemType Directory -Force $stage | Out-Null

$files = @(
    "$BuildDir\$exeName.exe",
    "$BuildDir\SDL2.dll",
    "$BuildDir\dxcompiler.dll",
    "$BuildDir\dxil.dll",
    "COPYING",
    "THIRD_PARTY_NOTICES.md",
    "README.md"
)
foreach ($f in $files) {
    if (-not (Test-Path $f)) { throw "missing $f" }
    Copy-Item $f $stage
}
Copy-Item -Recurse "$BuildDir\assets" (Join-Path $stage "assets")

# License texts of the vendored libraries the executable contains. A missing source stops packaging
# rather than shipping without it.
$licenses = Join-Path $stage "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
python scripts/stage-licenses.py $licenses --build-dir $BuildDir
if ($LASTEXITCODE -ne 0) { throw "Dependency/font license staging failed" }

# Belt and braces: no dump, no save, no user config, no log in the staging folder.
$forbidden = Get-ChildItem -Recurse $stage | Where-Object {
    ($_.Extension -in ".z64", ".n64", ".v64", ".rom", ".bin", ".eep", ".sra", ".fla", ".pak", ".mpk", ".log") -or
    ($_.Name -in "hud.json", "graphics.json", "controls.json", "input.json", "general.json", "sound.json", "portable.txt")
}
if ($forbidden) {
    $forbidden | ForEach-Object { Write-Error "refusing to package $($_.FullName)" }
    throw "game data or user files found in the staging folder"
}

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

$pdb = Join-Path $BuildDir "$exeName.pdb"
if (Test-Path $pdb) {
    $symbols = Join-Path $OutDir "$name-debug-symbols.zip"
    if (Test-Path $symbols) { Remove-Item $symbols }
    Compress-Archive -Path $pdb -DestinationPath $symbols
    Write-Host "symbols: $symbols"
}

Write-Host "package: $zip"
$stageFull = (Resolve-Path $stage).Path
Get-ChildItem -Recurse $stage | Where-Object { -not $_.PSIsContainer } |
    ForEach-Object { "{0,10:N0}  {1}" -f $_.Length, $_.FullName.Substring($stageFull.Length + 1) }
