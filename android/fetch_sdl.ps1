# Downloads the SDL2 source the Android build compiles (third_party/SDL2-2.32.10-src).
# The Windows build doesn't need it: it uses the prebuilt libraries in
# third_party/SDL2-2.32.10.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root "third_party\SDL2-2.32.10-src"
if (Test-Path (Join-Path $dest "CMakeLists.txt")) { Write-Host "SDL2 source already in $dest"; exit 0 }
$zip = Join-Path $env:TEMP "SDL2-2.32.10.zip"
Invoke-WebRequest "https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip" -OutFile $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($hash -ne "12b2dc2eb8f2836100a7916b5d394a0c82f1f7e32693f95f98305403af242f08") { throw "SDL2 download has an unexpected checksum ($hash)" }
$tmp = Join-Path $env:TEMP "sdl2src"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
Expand-Archive $zip $tmp
Move-Item (Join-Path $tmp "SDL2-2.32.10") $dest
Remove-Item -Recurse -Force $tmp, $zip
Write-Host "SDL2 source ready in $dest"
