# Stream the BAR decomp's ELF out of WSL into elf/.
param(
  [string]$Distro    = "Ubuntu-24.04",
  [string]$DecompDir = "/home/brysl/projects/bar-decomp",
  [string]$ElfRel    = "build/beetleadventurerac.us.elf"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
New-Item -ItemType Directory -Force -Path elf | Out-Null
$dest = Join-Path "elf" (Split-Path $ElfRel -Leaf)

wsl.exe -d $Distro -- test -f "$DecompDir/$ElfRel"
if ($LASTEXITCODE -ne 0) {
  throw "ELF not found in WSL. Build the decomp first: (WSL) cd $DecompDir && source .venv/bin/activate && make -j6"
}

Write-Host ">> Streaming $DecompDir/$ElfRel out of WSL ($Distro) -> $dest"
# cmd handles binary redirection cleanly.
cmd /c "wsl.exe -d $Distro -- cat $DecompDir/$ElfRel > `"$dest`""
Write-Host (">> ELF in place: {0} ({1} bytes)" -f $dest, (Get-Item $dest).Length)
