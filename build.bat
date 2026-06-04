@echo off
setlocal EnableDelayedExpansion

:: ---- Find Visual Studio automatically via vswhere ---------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo ERROR: vswhere.exe not found.
    echo Install Visual Studio 2019/2022 with the "Desktop development with C++" workload.
    exit /b 1
)

"!VSWHERE!" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\_sdvx_vs.txt" 2>nul
set /p VS_PATH=<"%TEMP%\_sdvx_vs.txt"
del "%TEMP%\_sdvx_vs.txt" >nul 2>nul

if "!VS_PATH!"=="" (
    echo ERROR: No Visual Studio installation with C++ tools found.
    echo Install the "Desktop development with C++" workload in the VS Installer.
    exit /b 1
)

call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to initialize VS x64 build environment.
    exit /b 1
)

:: ---- Output directory -------------------------------------------------------
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%

if not exist "%ROOT%\build" mkdir "%ROOT%\build"
cd /d "%ROOT%\build"

:: ---- Compile MinHook sources ------------------------------------------------
echo [1/3] Compiling MinHook...

cl /c /nologo /O2 /W0 /MT /GS- ^
    /I"%ROOT%\MinHook\include" ^
    "%ROOT%\MinHook\src\buffer.c" ^
    "%ROOT%\MinHook\src\hook.c" ^
    "%ROOT%\MinHook\src\trampoline.c" ^
    "%ROOT%\MinHook\src\hde\hde64.c"
if errorlevel 1 goto fail

lib /nologo /OUT:minhook.lib buffer.obj hook.obj trampoline.obj hde64.obj
if errorlevel 1 goto fail

:: ---- Compile debug UI -------------------------------------------------------
echo [2/3] Compiling sdvx_ap_debug.exe...

cl /nologo /O2 /W3 /MT /GS /EHsc /std:c++17 /DUNICODE /D_UNICODE ^
    "%ROOT%\src\debug_ui.cpp" ^
    /Fe"sdvx_ap_debug.exe" ^
    /link ^
    /SUBSYSTEM:WINDOWS ^
    /MACHINE:X64 ^
    kernel32.lib user32.lib gdi32.lib
if errorlevel 1 goto fail

:: ---- Compile DLL ------------------------------------------------------------
echo [3/3] Compiling version.dll...

cl /LD /nologo /O2 /W3 /MT /GS /EHsc /std:c++17 ^
    /I"%ROOT%\src" ^
    /I"%ROOT%\include" ^
    /I"%ROOT%\MinHook\include" ^
    "%ROOT%\src\dllmain.cpp" ^
    "%ROOT%\src\config.cpp" ^
    "%ROOT%\src\ap_client.cpp" ^
    "%ROOT%\src\hooks.cpp" ^
    /Fe"version.dll" ^
    /link ^
    /DEF:"%ROOT%\src\exports.def" ^
    /MACHINE:X64 ^
    minhook.lib winhttp.lib ws2_32.lib ^
    kernel32.lib user32.lib advapi32.lib
if errorlevel 1 goto fail

:: ---- Copy to deploy\ --------------------------------------------------------
if not exist "%ROOT%\deploy" mkdir "%ROOT%\deploy"
copy /Y "%ROOT%\build\version.dll"       "%ROOT%\deploy\version.dll"       >nul
copy /Y "%ROOT%\build\sdvx_ap_debug.exe" "%ROOT%\deploy\sdvx_ap_debug.exe" >nul

echo.
echo === Build SUCCESS ===
echo Deploy folder: %ROOT%\deploy\
echo   Drag into game modules\ folder:
echo     version.dll
echo     sdvx_ap_debug.exe
echo     archipelago.ini  ^(edit slot = YourName before launching the game^)
echo.
cd /d "%ROOT%"
exit /b 0

:fail
echo.
echo === Build FAILED ===
cd /d "%ROOT%"
exit /b 1
