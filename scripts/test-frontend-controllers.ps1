param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'frontend-controllers'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$entry = $entries | Where-Object { $_.file -match 'recompinput[/\\]src[/\\]input_mapping.cpp$' } | Select-Object -First 1
if (-not $entry) { throw 'Configure a frontend build first.' }
$productionObject = Join-Path $scratch 'input_mapping.obj'
$testObject = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$testSource = Join-Path $root 'tests/frontend_controller_regression.cpp'
$production = New-RegressionCompile $entry $entry.file $productionObject (Join-Path $scratch 'production.pdb')
$test = New-RegressionCompile $entry $testSource $testObject (Join-Path $scratch 'test.pdb')
# The mapper entry is a dependency target and does not include the host source tree.
$test = $test.Replace(' -c --', ' /I"' + (Join-Path $root 'src') + '" -c --')
Push-Location $build
try {
    foreach ($command in @($production, $test)) {
        & cmd.exe /d /s /c $command
        if ($LASTEXITCODE -ne 0) { throw 'Controller regression compilation failed.' }
    }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" $testObject $productionObject /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'Controller regression link failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Controller regressions failed.' }
} finally { Pop-Location }
