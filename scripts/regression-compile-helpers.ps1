# Compile regression objects separately from the application's Ninja outputs.
# A failed before/after experiment must never leave old code in a release object.
function New-RegressionCompile {
    param($Entry, [string]$Source, [string]$Object, [string]$Pdb)
    $line = $Entry.command.Replace($Entry.file.Replace('/', '\'), $Source).Replace($Entry.file, $Source)
    $outputPattern = '/Fo(?:"[^"]+"|\S+)'
    if ([regex]::Matches($line, $outputPattern).Count -ne 1) {
        throw 'Expected exactly one clang-cl object output in compile_commands.json.'
    }
    $line = [regex]::Replace($line, $outputPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) '/Fo"' + $Object + '"' })
    $line = [regex]::Replace($line, '/Fd(?:"[^"]+"|\S+)', [System.Text.RegularExpressions.MatchEvaluator]{ param($match) '/Fd"' + $Pdb + '"' })
    return $line.Replace(' -c --', ' /Gy /Gw -c --')
}

function New-RegressionWorkspace {
    param([string]$Root, [string]$Name)
    $scratch = Join-Path $Root ('.review/' + $Name + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $scratch -ErrorAction Stop | Out-Null
    return $scratch
}
