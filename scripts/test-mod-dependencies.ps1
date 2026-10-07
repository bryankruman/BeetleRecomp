param([string]$BuildDir = 'build-frontend', [string]$ManifestSource = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'mod-dependencies'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$names = @('mod_manifest.cpp', 'config.cpp', 'config_option.cpp')
$selected = foreach ($name in $names) {
    $entry = $entries | Where-Object { $_.file.Replace('\', '/').EndsWith('/librecomp/src/' + $name) } | Select-Object -First 1
    if (-not $entry) { throw "Configure a frontend build first: missing $name" }
    $entry
}
$obj = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$source = Join-Path $root 'tests/mod_dependencies_regression.cpp'
$entry = $selected[0]
$testCommand = New-RegressionCompile $entry $source $obj (Join-Path $scratch 'test.pdb')
$objects = @($obj)
$commands = foreach ($production in $selected) {
    $productionObj = Join-Path $scratch ([IO.Path]::GetFileNameWithoutExtension($production.file) + '.obj')
    $objects += $productionObj
    $productionSource = $production.file
    if ($ManifestSource -and $production -eq $entry) { $productionSource = $ManifestSource }
    New-RegressionCompile $production $productionSource $productionObj (Join-Path $scratch 'production.pdb')
}
Push-Location $build
try {
    foreach ($line in @($commands) + @($testCommand)) {
        & cmd.exe /d /s /c $line
        if ($LASTEXITCODE -ne 0) { throw 'Mod dependency regression compilation failed.' }
    }
    & 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo "/Fe$exe" @objects /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'Mod dependency regression link failed.' }
} finally { Pop-Location }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Mod dependency regressions failed.' }
