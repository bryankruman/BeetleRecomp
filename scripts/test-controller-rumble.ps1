param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'controller-rumble'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$entry = $entries | Where-Object { $_.file -match 'recompinput[/\\]src[/\\]input_mapping.cpp$' } | Select-Object -First 1
if (-not $entry) { throw 'Configure a frontend build first.' }
$productionObject = Join-Path $scratch 'rumble.obj'
$testObject = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$testSource = Join-Path $root 'tests/controller_rumble_regression.cpp'
$productionSource = Join-Path $root 'lib/RecompFrontend/recompinput/src/rumble.cpp'
$production = New-RegressionCompile $entry $productionSource $productionObject (Join-Path $scratch 'production.pdb')
$test = New-RegressionCompile $entry $testSource $testObject (Join-Path $scratch 'test.pdb')
# The mapper entry is a dependency target and does not include the host source tree.
$test = $test.Replace(' -c --', ' /I"' + (Join-Path $root 'src') + '" -c --')
Push-Location $build
try {
    foreach ($command in @($production, $test)) {
        & cmd.exe /d /s /c $command
        if ($LASTEXITCODE -ne 0) { throw 'Controller rumble regression compilation failed.' }
    }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" $testObject $productionObject /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'Controller rumble regression link failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Controller rumble regressions failed.' }
} finally { Pop-Location }
