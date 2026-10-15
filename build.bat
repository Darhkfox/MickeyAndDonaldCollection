@echo off
rem Builds the PC port (Release) with Visual Studio 2022 / Build Tools and CMake.
setlocal
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release --parallel || goto :error
echo.
echo Build finished: build\bin\DisneyClassics.exe
echo Run package.bat to assemble the distributable folder.
exit /b 0
:error
echo Build failed.
exit /b 1
