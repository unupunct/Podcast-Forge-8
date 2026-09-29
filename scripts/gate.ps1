# Stage gate: configure (if needed), build Release x64, run unit + harness tests.
# Usage:  .\scripts\gate.ps1 [-Live] [-VerifyUi]
param([switch]$Live, [switch]$VerifyUi, [switch]$Reconfigure)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $root
Set-Location $root
. (Join-Path $root 'scripts\env.ps1') *> $null

if ($Reconfigure -or -not (Test-Path 'build\release\build.ninja')) {
    cmake --preset release | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { Write-Output 'GATE: configure FAILED'; exit 1 }
}
$buildOut = cmake --build --preset release 2>&1
$buildOut | Select-String -Pattern ': error|: warning C|FAILED:|fatal error|LNK\d{4}' | Select-Object -First 40 | ForEach-Object { $_.Line }
if ($LASTEXITCODE -ne 0) { Write-Output 'GATE: build FAILED'; exit 1 }

$testOut = ctest --preset release 2>&1
$testOut | Select-String -Pattern 'tests passed|Failed|\*\*\*' | ForEach-Object { $_.Line }
if ($LASTEXITCODE -ne 0) { Write-Output 'GATE: tests FAILED'; exit 1 }

if ($Live) {
    $liveOut = ctest --preset live 2>&1
    $liveOut | Select-String -Pattern 'tests passed|Failed|\*\*\*|Skipped' | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) { Write-Output 'GATE: live tests FAILED'; exit 1 }
}

if ($VerifyUi) {
    $exe = Get-ChildItem 'build\release\src\app' -Recurse -Filter 'PodcastForge8.exe' | Select-Object -First 1
    $p = Start-Process $exe.FullName -ArgumentList '--verify-ui' -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { Write-Output "GATE: verify-ui FAILED ($($p.ExitCode))"; exit 1 }
    Write-Output 'verify-ui passed'
}
Write-Output 'GATE: OK'
exit 0
