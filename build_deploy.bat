@echo off
setlocal EnableDelayedExpansion

set "DEPLOY_DIR=C:\Games\SOUND VOLTEX EXCEED GEAR\game\modules"

:: ---- Build ------------------------------------------------------------------
call "%~dp0build.bat"
if errorlevel 1 goto fail

:: ---- Deploy -----------------------------------------------------------------
echo Deploying to %DEPLOY_DIR%...

if not exist "%DEPLOY_DIR%\" goto no_dir

copy /Y "%~dp0deploy\version.dll" "%DEPLOY_DIR%\version.dll" >nul
if errorlevel 1 goto copy_fail

copy /Y "%~dp0deploy\sdvx_ap_debug.exe" "%DEPLOY_DIR%\sdvx_ap_debug.exe" >nul
if errorlevel 1 goto copy_fail

echo.
echo === Deploy SUCCESS ===
echo %DEPLOY_DIR%\version.dll
echo %DEPLOY_DIR%\sdvx_ap_debug.exe
echo.
timeout /t 5 /nobreak >nul
exit /b 0

:no_dir
echo ERROR: Deploy directory not found: %DEPLOY_DIR%
goto fail

:copy_fail
echo ERROR: Copy failed -- is the game running?
goto fail

:fail
echo.
echo === Build/Deploy FAILED ===
echo.
timeout /t 5 /nobreak >nul
exit /b 1
