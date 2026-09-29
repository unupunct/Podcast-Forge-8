# Fetch pinned third-party dependencies into third_party/ (not committed).
$ErrorActionPreference = 'Stop'
$tp = Join-Path $PSScriptRoot '..\third_party'
New-Item -ItemType Directory -Force $tp | Out-Null
Push-Location $tp
try {
    if (-not (Test-Path JUCE))   { git -c advice.detachedHead=false clone --depth 1 --branch 8.0.15  https://github.com/juce-framework/JUCE.git JUCE }
    if (-not (Test-Path Catch2)) { git -c advice.detachedHead=false clone --depth 1 --branch v3.16.0 https://github.com/catchorg/Catch2.git Catch2 }
    if (-not (Test-Path sqlite)) {
        Invoke-WebRequest https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip -OutFile sqlite.zip
        Expand-Archive sqlite.zip -DestinationPath . ; Rename-Item sqlite-amalgamation-3530400 sqlite ; Remove-Item sqlite.zip
    }
} finally { Pop-Location }
