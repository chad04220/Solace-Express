@echo off
rem Air Xpress development captures: renders test scenes on your GPU and saves them as PNGs in the "shots"
rem folder next to AirXpress.exe. Takes under a minute once the shaders are cached.
cd /d "%~dp0"
set WRBOMB=1
start "" /wait AirXpress.exe --shots menu,loading,loadingready,loadingair,gps,cockpit,rjetc,wr_8_0_-55_0_14,wr_8_0_-55_0_18,crash1_8,crash1_30,airbreak1.0,airbreak2.5 --size 1920x1080
echo.
echo Done. The images are in the "shots" folder:
echo %~dp0shots
start "" "%~dp0shots"
pause
