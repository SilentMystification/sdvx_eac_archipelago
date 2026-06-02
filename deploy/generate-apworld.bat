@echo off
setlocal EnableDelayedExpansion

:: ---- Configurable paths -------------------------------------------------------
set "GAME_DIR=C:\Games\SOUND VOLTEX EXCEED GEAR"
:: set "AP_WORLDS=C:\ProgramData\Archipelago\lib\worlds"

:: ---- Internal paths (relative to this script) ---------------------------------
set "TOOLS=%~dp0tools"
set "OUT=%~dp0ap_world"
set "MUSIC_DB=%TOOLS%\music_db.xml"

:: ---- [1/2] Extract music_db.xml -----------------------------------------------
echo [1/2] Extracting music_db.xml from game data...
"%TOOLS%\extdrm.exe" get "%GAME_DIR%\game\data" /others/music_db.xml -o "%MUSIC_DB%"
if errorlevel 1 goto fail

:: ---- [2/2] Generate AP world data ---------------------------------------------
echo [2/2] Generating AP world data...
if not exist "%OUT%" mkdir "%OUT%"
python "%TOOLS%\generate_song_data.py" "%MUSIC_DB%" "%OUT%\data.py"
if errorlevel 1 goto fail

echo.
echo === Done ===
echo   %OUT%\data.py updated.
echo.
exit /b 0

:fail
echo.
echo === FAILED ===
exit /b 1
