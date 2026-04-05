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

call "%~dp0setup_build.bat" %CONFIG%
if errorlevel 1 (
    echo [ERROR] Setup/build failed.
    exit /b 1
)

call "%~dp0run_utm.bat" %CONFIG%
exit /b %errorlevel%

:help
echo Usage: setup_and_run.bat [Debug^|Release]
echo Default: Release
exit /b 0
