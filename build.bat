@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BUILD_TYPE=Release"
set "BUILD_DIR=%ROOT%\build-release"
set "RUN_TESTS=1"
set "FRESH=0"

if /I "%~1"=="debug" (
    set "BUILD_TYPE=Debug"
    set "BUILD_DIR=%ROOT%\build"
)
if /I "%~1"=="release" (
    set "BUILD_TYPE=Release"
    set "BUILD_DIR=%ROOT%\build-release"
)
if /I "%~1"=="clean" (
    set "FRESH=1"
)
if /I "%~2"=="clean" (
    set "FRESH=1"
)
if /I "%~1"=="notest" set "RUN_TESTS=0"
if /I "%~2"=="notest" set "RUN_TESTS=0"

if defined QT_DIR (
    set "QT_PATH=%QT_DIR%"
) else if exist "C:\Qt\6.10.2\msvc2022_64" (
    set "QT_PATH=C:\Qt\6.10.2\msvc2022_64"
) else (
    echo [ERROR] Qt kit not found. Set QT_DIR to your Qt msvc kit path.
    exit /b 1
)

if defined VC_VARS_BAT (
    set "VCVARS=%VC_VARS_BAT%"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo [ERROR] vcvars64.bat not found. Set VC_VARS_BAT to the full path.
    exit /b 1
)

if "%FRESH%"=="1" (
    echo [INFO] Removing previous build directory: "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

echo [INFO] Root:       %ROOT%
echo [INFO] Qt:         %QT_PATH%
echo [INFO] Toolchain:  %VCVARS%
echo [INFO] Build type: %BUILD_TYPE%
echo [INFO] Build dir:  %BUILD_DIR%

call "%VCVARS%"
if errorlevel 1 exit /b 1

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_PREFIX_PATH="%QT_PATH%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DJARVIS_BUILD_TESTS=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 exit /b 1

if "%RUN_TESTS%"=="1" (
    ctest --test-dir "%BUILD_DIR%" --output-on-failure
    if errorlevel 1 exit /b 1
)

echo.
echo [OK] Build complete.
echo [OK] Vaxil executable: "%ROOT%\bin\vaxil.exe"
echo [OK] Logs:       "%ROOT%\bin\logs"
exit /b 0
