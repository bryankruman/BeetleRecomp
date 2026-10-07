param([string]$BuildDir = 'build-frontend')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'regression-compile-helpers.ps1')
$scratch = New-RegressionWorkspace $root 'runtime-input'
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$entries = Get-Content -Raw (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$inputEntry = $entries | Where-Object { $_.file -match 'src[/\\]main[/\\]bar_input.cpp$' } | Select-Object -First 1
$profilesEntry = $entries | Where-Object { $_.file -match 'recompinput[/\\]src[/\\]profiles.cpp$' } | Select-Object -First 1
$playersEntry = $entries | Where-Object { $_.file -match 'recompinput[/\\]src[/\\]players.cpp$' } | Select-Object -First 1
if (-not $inputEntry -or -not $profilesEntry -or -not $playersEntry) { throw 'Configure a frontend build first.' }
$obj = Join-Path $scratch 'test.obj'
$exe = Join-Path $scratch 'test.exe'
$testSource = Join-Path $root 'tests/runtime_input_regression.cpp'
$testCommand = New-RegressionCompile $inputEntry $testSource $obj (Join-Path $scratch 'test.pdb')
$inputObj = Join-Path $scratch 'bar_input.obj'
$profilesObj = Join-Path $scratch 'profiles.obj'
$playersObj = Join-Path $scratch 'players.obj'
$inputCommand = New-RegressionCompile $inputEntry $inputEntry.file $inputObj (Join-Path $scratch 'production.pdb')
$profilesCommand = New-RegressionCompile $profilesEntry $profilesEntry.file $profilesObj (Join-Path $scratch 'production.pdb')
$playersCommand = New-RegressionCompile $playersEntry $playersEntry.file $playersObj (Join-Path $scratch 'production.pdb')
$sdl = Join-Path $build '_deps/sdl2-src/lib/x64'
$compiler = 'C:\Program Files\LLVM\bin\clang-cl.exe'
Push-Location $build
try {
    foreach ($command in @($inputCommand, $profilesCommand, $playersCommand, $testCommand)) {
        & cmd.exe /d /s /c $command
        if ($LASTEXITCODE -ne 0) { throw 'Regression test compilation failed.' }
    }
    & $compiler /nologo "/Fe$exe" $obj $inputObj $profilesObj $playersObj (Join-Path $sdl 'SDL2.lib') /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'Regression test link failed.' }
} finally { Pop-Location }
$originalPath = $env:PATH
try {
    $env:PATH = "$sdl;$originalPath"
    Set-Location $root
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Runtime input regressions failed.' }
} finally { $env:PATH = $originalPath }
