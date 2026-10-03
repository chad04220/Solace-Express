@echo off
rem Solace Express loading-screen pictures: renders every airport, and every aircraft in flight, on your GPU and
rem saves them in the "loading" folder next to SolaceExpress.exe. The pre-flight loading screen then shows these
rem instead of the aerial map. Run it again after an update to refresh them. Takes a few minutes.
cd /d "%~dp0"
start "" /wait SolaceExpress.exe --loadshots
echo Done: the pictures are in the "loading" folder.
pause
