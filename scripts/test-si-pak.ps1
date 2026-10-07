param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'si-pak'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$entry = $entries | Where-Object { $_.file -match 'src[/\\]main[/\\]os_unimpl_stubs.cpp$' } | Select-Object -First 1
if (-not $entry) { throw 'Configure a frontend build first.' }
$obj = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$source = Join-Path $root 'tests/si_pak_regression.cpp'
$command = New-RegressionCompile $entry $source $obj (Join-Path $scratch 'test.pdb')
$productionObj = Join-Path $scratch 'os_unimpl_stubs.obj'
$productionCommand = New-RegressionCompile $entry $entry.file $productionObj (Join-Path $scratch 'production.pdb')
Push-Location $build
try {
    foreach ($line in @($productionCommand, $command)) {
        & cmd.exe /d /s /c $line
        if ($LASTEXITCODE -ne 0) { throw 'SI regression compilation failed.' }
    }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" $obj $productionObj /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'SI regression link failed.' }
} finally { Pop-Location }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'SI regressions failed.' }
