param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'pi-read'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$entry = $entries | Where-Object { $_.file -match 'src[/\\]main[/\\]os_unimpl_stubs.cpp$' } | Select-Object -First 1
if (-not $entry) { throw 'Configure a frontend build first.' }
$exe = Join-Path $scratch 'test.exe'
$objects = @()
Push-Location $build
try {
    foreach ($relative in @('src/main/bar_pi.cpp','tests/pi_read_regression.cpp')) {
        $obj = Join-Path $scratch ((Split-Path -Leaf $relative) + '.obj')
        $objects += $obj
        $line = New-RegressionCompile $entry (Join-Path $root $relative) $obj (Join-Path $scratch 'test.pdb')
        & cmd.exe /d /s /c $line
        if ($LASTEXITCODE -ne 0) { throw 'PI regression compilation failed.' }
    }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" @objects /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'PI regression link failed.' }
} finally { Pop-Location }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'PI regressions failed.' }
