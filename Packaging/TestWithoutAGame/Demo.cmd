@echo off
rem CyGPUInspector - demonstration with no game and no GPU.
rem Starts the synthetic producer, then the application, already connected to it.
setlocal
cd /d "%~dp0"

echo.
echo   Starting the fake session (no game needed)...
start "CyGPUInspector fake session" /min "%~dp0CyGPUInspectorFakeSession.exe"

rem Give the session time to publish itself before the application looks for it.
ping -n 3 127.0.0.1 >nul

echo   Starting CyGPUInspectorApp, connected...
start "" "%~dp0..\App\CyGPUInspectorApp.exe" --connect

echo.
echo   Press a key to stop the fake session and close this window.
pause >nul

taskkill /IM CyGPUInspectorFakeSession.exe /F >nul 2>&1
