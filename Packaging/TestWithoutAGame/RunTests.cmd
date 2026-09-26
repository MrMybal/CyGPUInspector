@echo off
rem Runs the four test suites and leaves the results on screen.
setlocal
cd /d "%~dp0"

for %%T in (CyGPUInspectorCoreTests CyGPUInspectorAssistantTests CyGPUInspectorShaderTests CyGPUInspectorIpcTests) do (
    echo ============================================================
    echo   %%T
    echo ============================================================
    "%~dp0%%T.exe"
    echo.
)

echo Done. Every suite should end with "0 failure(s)".
pause
