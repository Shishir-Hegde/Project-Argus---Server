@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ========================================================
echo   Building and Running Project Argus Server (Dual-Cam)
echo ========================================================

:: Add modern WinLibs GCC to PATH if present
if exist "C:\Users\aayus\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin" (
    set "PATH=C:\Users\aayus\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;!PATH!"
)

if not exist "build" mkdir "build"

echo [+] Compiling Project-Argus-Server...
g++ -std=c++17 -O2 -Iinclude -c src/main.cpp -o build/main.o ^
 && g++ -std=c++17 -O2 -Iinclude -c src/ReidMatcher.cpp -o build/ReidMatcher.o ^
 && g++ -std=c++17 -O2 -Iinclude -c src/CameraManager.cpp -o build/CameraManager.o ^
 && g++ -std=c++17 -O2 -Iinclude -c src/NetworkServer.cpp -o build/NetworkServer.o ^
 && g++ -std=c++17 -O2 -Iinclude -c src/GuiRenderer.cpp -o build/GuiRenderer.o ^
 && g++ build/main.o build/ReidMatcher.o build/CameraManager.o build/NetworkServer.o build/GuiRenderer.o -o Project-Argus-Server.exe -lws2_32 -lgdi32 -luser32

if %ERRORLEVEL% EQU 0 (
    echo [+] Build successful! Launching Project-Argus-Server.exe...
    Project-Argus-Server.exe
) else (
    echo [!] Build failed! Check compiler errors above.
    pause
    exit /b 1
)
