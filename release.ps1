# Makes the release zip, dist\Mickey-and-Donald-Collection-v<version>-win64.zip, holding only what is needed
# to play: the executable and the player's readme. No ROMs, settings, saves, screenshots or shortcuts.
# Build first with build.bat.   Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "build\bin\DisneyClassics.exe"
if (-not (Test-Path $exe)) { throw "Build first with build.bat" }
$version = (Select-String -Path (Join-Path $root "src\app\app.h") -Pattern 'kAppVersion = "([^"]+)"').Matches[0].Groups[1].Value
New-Item -ItemType Directory -Force (Join-Path $root "dist") | Out-Null
$zipPath = Join-Path $root "dist\Mickey-and-Donald-Collection-v$version-win64.zip"

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$files = @(
  @($exe, "DisneyClassics.exe"),
  @((Join-Path $root "readme.txt"), "readme.txt")
)
$stream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::Create)
$zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($f in $files) {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f[0], "Mickey and Donald Collection/" + $f[1],
      [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
} finally {
  $zip.Dispose()
  $stream.Dispose()
}
Write-Host "Release zip: $zipPath"
