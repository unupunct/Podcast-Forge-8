# Builds the Release, runs the full gate (tests + verify-ui) and compiles the installer.
# Output: out\PodcastForge8-<version>-Setup-x64.exe and out\PodcastForge8-<version>-x64.zip (portable)
# Requires Inno Setup 6 in tools\InnoSetup6 (portable copy; see docs/BUILDING.md).
param([switch]$SkipGate)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cm = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cm -notmatch 'project\(PodcastForge8 VERSION (\d+\.\d+\.\d+)') { throw 'version not found in CMakeLists.txt' }
$version = $Matches[1]
Write-Host "Podcast Forge 8 $version"

if (-not $SkipGate) {
    & (Join-Path $PSScriptRoot 'gate.ps1') -Reconfigure
    if ($LASTEXITCODE -ne 0) { throw 'gate failed: not packaging' }
}

$exe = Join-Path $root 'build\release\bin\PodcastForge8.exe'
if (-not (Test-Path $exe)) { throw "missing $exe" }
$fv = (Get-Item $exe).VersionInfo.ProductVersion
if ($fv -ne $version) { throw "exe reports version $fv, CMakeLists says $version (reconfigure)" }

$iscc = Join-Path $root 'tools\InnoSetup6\ISCC.exe'
if (-not (Test-Path $iscc)) { throw "Inno Setup not found at $iscc" }
New-Item -ItemType Directory -Force (Join-Path $root 'out') | Out-Null
& $iscc "/DAppVersion=$version" /Q (Join-Path $root 'installer\PodcastForge8.iss')
if ($LASTEXITCODE -ne 0) { throw 'ISCC failed' }

# Portable zip: the exe plus licence and docs (runs without installing).
$stage = Join-Path $root "out\PodcastForge8-$version-x64"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, (Join-Path $stage 'docs') | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'README.md') $stage
foreach ($d in 'LIMITATIONS.md', 'PROJECTS.md', 'OBS.md', 'CHANGELOG.md') {
    $p = Join-Path $root "docs\$d"
    if (Test-Path $p) { Copy-Item $p (Join-Path $stage 'docs') }
}
$zip = "$stage.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Remove-Item -Recurse -Force $stage

Get-ChildItem (Join-Path $root 'out') -Filter "PodcastForge8-$version*" | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    Write-Host ("{0}  {1:N1} MB  SHA256 {2}" -f $_.Name, ($_.Length / 1MB), $h)
}
Write-Host 'PACKAGE: OK'
