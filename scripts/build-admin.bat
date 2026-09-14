@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build\qt"
set "BUILD_TYPE=Debug"
set "CMAKE_BIN=cmake"
set "QT_PREFIX=%QT_PREFIX%"

if "%QT_PREFIX%"=="" (
    set "QMAKE="
    for /f "delims=" %%I in ('where qmake 2^>nul') do if not defined QMAKE set "QMAKE=%%I"
    if defined QMAKE (
        for %%A in ("%QMAKE%") do set "QT_BIN=%%~dpA"
        for %%A in ("%QT_BIN%..") do set "QT_PREFIX=%%~fA"
    )
)

where ninja >nul 2>nul
if errorlevel 1 goto use_vs

set "CMAKE_ARGS=-S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DSMARTPARK_BUILD_ADMIN=ON -DBUILD_TESTING=ON"
if not "%QT_PREFIX%"=="" set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH="%QT_PREFIX%""
"%CMAKE_BIN%" %CMAKE_ARGS%
if errorlevel 1 exit /b 1
"%CMAKE_BIN%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 exit /b 1
set "APP=%BUILD_DIR%\apps\admin\smartpark_admin.exe"
goto check

:use_vs
echo Ninja not found; using Visual Studio generator.
set "CMAKE_ARGS=-S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DSMARTPARK_BUILD_ADMIN=ON -DBUILD_TESTING=ON"
if not "%QT_PREFIX%"=="" set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH="%QT_PREFIX%""
"%CMAKE_BIN%" %CMAKE_ARGS%
if errorlevel 1 exit /b 1
"%CMAKE_BIN%" --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 exit /b 1
set "APP=%BUILD_DIR%\apps\admin\%BUILD_TYPE%\smartpark_admin.exe"

:check
if not "%QT_PREFIX%"=="" echo Using Qt prefix: %QT_PREFIX%
if exist "%APP%" (
    echo Build OK: "%APP%"
    exit /b 0
)
echo Error: built executable not found: "%APP%" >&2
exit /b 1
