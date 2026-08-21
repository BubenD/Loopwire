@echo off
REM TAKE THE HOUSE STYLE OFF THIS MACHINE AND INTO THE SOURCE TREE.
REM
REM The default models and the parts library are drawn and edited in the running application, which keeps them
REM in the machine's own app-data folder. They also SHIP with the application - compiled into the executable,
REM so a fresh install draws in the house style from its first drawing instead of from whenever somebody gets
REM round to drawing all of them again. Those two copies have to be brought level whenever the house style
REM changes, and doing it by hand is how one of them ends up a month behind the other.
REM
REM Run this after saving models in the manager or parts in the library, then BUILD - the files are read into
REM the executable at build time - and commit what changed.
setlocal
set "SRC=%APPDATA%\Whatson\Edraw"
set "DST=%~dp0resources\defaults"
if not exist "%DST%" mkdir "%DST%"

if not exist "%SRC%\mech-models.json" (
    echo No default models on this machine yet: %SRC%\mech-models.json
) else (
    copy /Y "%SRC%\mech-models.json" "%DST%\mech-models.json" >nul
    if errorlevel 1 (echo FAILED to copy the models & exit /b 1)
    echo models  : %SRC%\mech-models.json
)
if not exist "%SRC%\library\edrawlib.plb" (
    echo No parts library on this machine yet: %SRC%\library\edrawlib.plb
) else (
    copy /Y "%SRC%\library\edrawlib.plb" "%DST%\edrawlib.plb" >nul
    if errorlevel 1 (echo FAILED to copy the library & exit /b 1)
    echo library : %SRC%\library\edrawlib.plb
)

echo.
echo Into: %DST%
echo.
REM What actually changed, said plainly - "copied" is not the same as "different", and a run that changed
REM nothing should look like a run that changed nothing.
git -C "%~dp0." status --short -- resources/defaults
echo.
echo Now build (the files are read into the executable at build time), then commit.
endlocal
