# Enter the VS 2022 x64 developer environment and put the bundled CMake + Ninja on PATH.
# Usage:  . .\scripts\env.ps1
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -products * -latest -property installationPath
if (-not $vs) { throw "Visual Studio 2022 (Build Tools) not found" }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
$ide = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$ide\CMake\bin;$ide\Ninja;$env:PATH"
