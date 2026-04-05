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
set "EXE_PATH=%ROOT%build\%CONFIG%\UltimateTaskManager.exe"

if not exist "%EXE_PATH%" (
    echo [WARN] Executable not found at:
    echo        "%EXE_PATH%"
    echo [STEP] Building first...
    call "%ROOT%setup_build.bat" %CONFIG%
    if errorlevel 1 (
        echo [ERROR] Build step failed. Cannot launch.
        exit /b 1
    )
)

where powershell >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PowerShell was not found. Cannot launch elevated.
    exit /b 1
)

echo [STEP] Launching UltimateTaskManager as Administrator...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%EXE_PATH%' -Verb RunAs"
if errorlevel 1 (
    echo [ERROR] Launch failed.
    exit /b 1
)

echo [OK] Launch command sent.
exit /b 0

:help
echo Usage: run_utm.bat [Debug^|Release]
echo Default: Release
exit /b 0
