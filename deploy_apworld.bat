@echo off
setlocal

set "AP_WORLDS=C:\ProgramData\Archipelago\lib\worlds\sdvx"
set "SRC=%~dp0ap_world"

echo Deploying SDVX AP world to %AP_WORLDS% ...
if not exist "%AP_WORLDS%" mkdir "%AP_WORLDS%"

copy /Y "%SRC%\__init__.py" "%AP_WORLDS%\__init__.py"
if errorlevel 1 goto fail
copy /Y "%SRC%\data.py" "%AP_WORLDS%\data.py"
if errorlevel 1 goto fail

echo Done.
exit /b 0

:fail
echo ERROR: Copy failed.
exit /b 1
