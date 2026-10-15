@echo off
rem Assembles dist\ with the launcher executable, per-game shortcuts, the user's ROMs and docs.
setlocal
cd /d "%~dp0"
if not exist build\bin\DisneyClassics.exe (
    echo Build first with build.bat
    exit /b 1
)
set "OUT=dist\Mickey and Donald Collection"
rem Replace only what the package ships; settings.ini, saves\ and screenshots\ in there are the player's.
if not exist "%OUT%\roms" mkdir "%OUT%\roms"
del /q "%OUT%\*.lnk" 2>nul
copy /y build\bin\DisneyClassics.exe "%OUT%\" >nul
copy /y README.md "%OUT%\README.md" >nul
copy /y readme.txt "%OUT%\readme.txt" >nul
for %%f in (*.bin *.md *.gen *.smd) do (
    if /i not "%%~nxf"=="README.md" copy /y "%%f" "%OUT%\roms\" >nul
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = New-Object -ComObject WScript.Shell; $d = (Resolve-Path '%OUT%').Path;" ^
  "foreach ($g in @(@('Castle of Illusion','coi'), @('QuackShot','qs'), @('World of Illusion','woi'))) {" ^
  "  $l = $s.CreateShortcut((Join-Path $d ($g[0] + '.lnk'))); $l.TargetPath = (Join-Path $d 'DisneyClassics.exe');" ^
  "  $l.Arguments = '--game ' + $g[1]; $l.WorkingDirectory = $d; $l.IconLocation = (Join-Path $d 'DisneyClassics.exe') + ',0'; $l.Save() }"
echo Packaged to "%OUT%"
exit /b 0
