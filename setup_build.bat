@echo off
setlocal EnableExtensions

if /I "%~1"=="/?" goto :help
if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

set "CONFIG=%~1"
if /I "%CONFIG%"=="" set "CONFIG=Release"

if /I not "%CONFIG%"=="Debug" if /I not "%CONFIG%"=="Release" (
    echo [ERROR] Invalid configuration: "%CONFIG%"
    echo         Use Debug or Release.
    exit /b 1
)

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"
set "GENERATOR=Visual Studio 17 2022"
set "ARCH=x64"
set "EXE_PATH=%BUILD_DIR%\%CONFIG%\UltimateTaskManager.exe"
set "CMAKE_EXE="
set "VS_FOUND="

echo [INFO] Project root: %ROOT%

for /f "delims=" %%I in ('where cmake 2^>nul') do (
    set "CMAKE_EXE=%%I"
    goto :cmake_found
)

if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if "%CMAKE_EXE%"=="" if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles(x86)%\CMake\bin\cmake.exe"

:cmake_found
if "%CMAKE_EXE%"=="" (
    echo [ERROR] CMake was not found in PATH or common install locations.
    echo         Install with: winget install Kitware.CMake
    echo         Then reopen terminal and run this script again.
    exit /b 1
)

echo [INFO] Using CMake: "%CMAKE_EXE%"

for %%E in (BuildTools Community Professional Enterprise) do (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\MSBuild\Current\Bin\MSBuild.exe" set "VS_FOUND=1"
)

if "%VS_FOUND%"=="" (
    echo [ERROR] Visual Studio 2022 C++ build tools were not found.
    echo         Install them with:
    echo         winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
    echo         Then reopen terminal and run this script again.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [STEP] Configure CMake...
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH%
if errorlevel 1 (
    echo [ERROR] Configure failed.
    exit /b 1
)

echo [STEP] Build %CONFIG%...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo [ERROR] Build succeeded but executable was not found:
    echo         "%EXE_PATH%"
    exit /b 1
)

echo [OK] Build complete:
echo      "%EXE_PATH%"
exit /b 0

:help
echo Usage: setup_build.bat [Debug^|Release]
echo Default: Release
exit /b 0
