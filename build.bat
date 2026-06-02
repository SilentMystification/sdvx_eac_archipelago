@echo off
setlocal EnableDelayedExpansion

:: ---- Toolchain paths (quoted internally where used) -----------------------
set "MSVC_VER=14.44.35207"
set "VS_BASE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "MSVC_DIR=%VS_BASE%\VC\Tools\MSVC\%MSVC_VER%"
set "WINSDK_VER=10.0.26100.0"
set "WINSDK_BASE=C:\Program Files (x86)\Windows Kits\10"

set "CL_EXE=%MSVC_DIR%\bin\Hostx64\x64\cl.exe"
set "LIB_EXE=%MSVC_DIR%\bin\Hostx64\x64\lib.exe"

set "MSVC_INC=%MSVC_DIR%\include"
set "MSVC_LIB=%MSVC_DIR%\lib\x64"
set "SDK_INC=%WINSDK_BASE%\Include\%WINSDK_VER%"
set "SDK_LIB=%WINSDK_BASE%\Lib\%WINSDK_VER%"

set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%

:: ---- Output directory ------------------------------------------------------
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
cd /d "%ROOT%\build"

:: ---- Compile MinHook sources -----------------------------------------------
echo [1/2] Compiling MinHook...

"%CL_EXE%" /c /nologo /O2 /W0 /MT /GS- ^
    /I"%ROOT%\MinHook\include" ^
    /I"%MSVC_INC%" ^
    /I"%SDK_INC%\ucrt" ^
    /I"%SDK_INC%\shared" ^
    /I"%SDK_INC%\um" ^
    "%ROOT%\MinHook\src\buffer.c" ^
    "%ROOT%\MinHook\src\hook.c" ^
    "%ROOT%\MinHook\src\trampoline.c" ^
    "%ROOT%\MinHook\src\hde\hde64.c"
if errorlevel 1 goto fail

"%LIB_EXE%" /nologo /OUT:minhook.lib buffer.obj hook.obj trampoline.obj hde64.obj
if errorlevel 1 goto fail

:: ---- Compile debug UI -------------------------------------------------------
echo [2/3] Compiling sdvx_ap_debug (sdvx_ap_debug.exe)...

"%CL_EXE%" /nologo /O2 /W3 /MT /GS /EHsc /std:c++17 /DUNICODE /D_UNICODE ^
    /I"%MSVC_INC%" ^
    /I"%SDK_INC%\ucrt" ^
    /I"%SDK_INC%\shared" ^
    /I"%SDK_INC%\um" ^
    "%ROOT%\src\debug_ui.cpp" ^
    /Fe"sdvx_ap_debug.exe" ^
    /link ^
    /SUBSYSTEM:WINDOWS ^
    /MACHINE:X64 ^
    /LIBPATH:"%MSVC_LIB%" ^
    /LIBPATH:"%SDK_LIB%\ucrt\x64" ^
    /LIBPATH:"%SDK_LIB%\um\x64" ^
    kernel32.lib user32.lib gdi32.lib
if errorlevel 1 goto fail

:: ---- Compile DLL sources ---------------------------------------------------
echo [3/3] Compiling sdvx_archipelago (version.dll)...

"%CL_EXE%" /LD /nologo /O2 /W3 /MT /GS /EHsc /std:c++17 ^
    /I"%ROOT%\src" ^
    /I"%ROOT%\include" ^
    /I"%ROOT%\MinHook\include" ^
    /I"%MSVC_INC%" ^
    /I"%SDK_INC%\ucrt" ^
    /I"%SDK_INC%\shared" ^
    /I"%SDK_INC%\um" ^
    "%ROOT%\src\dllmain.cpp" ^
    "%ROOT%\src\config.cpp" ^
    "%ROOT%\src\ap_client.cpp" ^
    "%ROOT%\src\hooks.cpp" ^
    /Fe"version.dll" ^
    /link ^
    /DEF:"%ROOT%\src\exports.def" ^
    /MACHINE:X64 ^
    /LIBPATH:"%MSVC_LIB%" ^
    /LIBPATH:"%SDK_LIB%\ucrt\x64" ^
    /LIBPATH:"%SDK_LIB%\um\x64" ^
    minhook.lib winhttp.lib ws2_32.lib ^
    kernel32.lib user32.lib advapi32.lib
if errorlevel 1 goto fail

:: ---- Stage deployable files to deploy\ -------------------------------------
if not exist "%ROOT%\deploy" mkdir "%ROOT%\deploy"
copy /Y "%ROOT%\build\version.dll"       "%ROOT%\deploy\version.dll"       >nul
copy /Y "%ROOT%\build\sdvx_ap_debug.exe" "%ROOT%\deploy\sdvx_ap_debug.exe" >nul


echo.
echo === Build SUCCESS ===
echo Deploy folder: %ROOT%\deploy\
echo   Game mod (drag into game modules\):
echo     version.dll
echo     sdvx_ap_debug.exe
echo   AP world (run generate-apworld.bat, then drag ap_world\sdvx\ into Archipelago worlds\)
echo.
cd /d "%ROOT%"
exit /b 0

:fail
echo.
echo === Build FAILED ===
cd /d "%ROOT%"
exit /b 1
