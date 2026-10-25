param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'n64-adapter'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$entry = $entries | Where-Object { $_.file -match 'src[/\\]main[/\\]bar_input.cpp$' } | Select-Object -First 1
if (-not $entry) { throw 'Configure a frontend build first.' }
$obj = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$command = New-RegressionCompile $entry (Join-Path $root 'tests/n64_adapter_regression.cpp') $obj (Join-Path $scratch 'test.pdb')
$sdl = Join-Path $build '_deps/sdl2-src/lib/x64'
Push-Location $build
try {
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Adapter regression compilation failed.' }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" $obj (Join-Path $sdl 'SDL2.lib') /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'Adapter regression link failed.' }
} finally { Pop-Location }
$originalPath = $env:PATH
try {
    $env:PATH = "$sdl;$originalPath"
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Adapter regressions failed.' }
} finally { $env:PATH = $originalPath }
