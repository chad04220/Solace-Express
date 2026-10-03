@echo off
rem Solace Express development captures: renders test scenes on your GPU and saves them as PNGs in the "shots"
rem folder next to SolaceExpress.exe. Takes under a minute once the shaders are cached.
cd /d "%~dp0"
start "" /wait SolaceExpress.exe --shots menu,loading,loadingready,loadingair,gps,cockpit,rjetc,wr_9_0_-55_0_5,wr_10_0_-55_0_1.2,crash1_8,crash1_30,airbreak1.0,airbreak2.5,jcam165_10_35,jcam165_10_65,jcam165_10_90,wr_7_0_0_0_0.5,wr_7_0_0_0_2.5,wr_7_0_0_0_5.5 --size 1920x1080
echo.
echo Done. The images are in the "shots" folder:
echo %~dp0shots
start "" "%~dp0shots"
pause
