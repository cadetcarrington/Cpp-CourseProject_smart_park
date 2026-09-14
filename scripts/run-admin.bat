@echo off
setlocal

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build\qt"
set "APP=%BUILD_DIR%\apps\admin\smartpark_admin.exe"

if not exist "%APP%" set "APP=%BUILD_DIR%\apps\admin\Debug\smartpark_admin.exe"
if not exist "%APP%" (
    echo Admin GUI not built: %APP%
    echo Run scripts\build-admin.bat first.
    exit /b 1
)

"%APP%" %*
