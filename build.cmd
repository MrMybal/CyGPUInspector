@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  build.cmd [Release^|Debug] [clean]
rem  Configures (Ninja + MSVC x64) and builds CyGPUInspector into bin\<Config>\
rem ---------------------------------------------------------------------------
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set ROOT=%~dp0
set BUILD_DIR=%ROOT%build\%CONFIG%

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
    echo Visual Studio with C++ tools not found
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

set CMAKE_EXE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA_EXE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

if "%2"=="clean" if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

"%CMAKE_EXE%" -S "%ROOT%." -B "%BUILD_DIR%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1
"%CMAKE_EXE%" --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1
echo.
echo Build OK -^> %ROOT%bin\%CONFIG%\
endlocal
