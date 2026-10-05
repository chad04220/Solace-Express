@echo off
rem Solace Express report: everything the renderer work needs from your GPU, in one run. Put this file next to
rem SolaceExpress.exe (the release zip's folder) and double-click it. It writes a "report" folder and zips it as
rem report_<version>.zip next to the exe: send that zip over. Takes 10-20 minutes; each window's title shows progress.
rem
rem   1. benchmark on the rasterizer (the default renderer) at 1920x1080 and at your native resolution
rem   2. the same benchmark on the ray tracer at 1920x1080, for comparison
rem   3. the pass-by-pass analysis on the rasterizer (heat maps in the analysis folder)
rem   4. screenshots on the rasterizer: the research terminal with the XR-10 and XR-20, the XR-20 outside with its bay
rem      open and from the seat, the UFO, the cockpits and jets, the hangar market
rem   5. startup.log (your GPU, texture units, which renderer programs compiled) and the settings file
cd /d "%~dp0"
if not exist SolaceExpress.exe (
  echo SolaceExpress.exe is not in this folder. Put report.bat next to it and run it again.
  pause
  exit /b 1
)
set VER=v3.14.0
if exist VERSION.txt set /p VER=<VERSION.txt
set OUT=report
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
echo [1/5] Benchmark on the rasterizer, 1920x1080 ...
start "" /wait SolaceExpress.exe --raster --bench menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1,research20,ckv11_0_-10_12,ufo13_0 --size 1920x1080 --out "%OUT%\bench_raster_1080p.txt"
echo [1/5] Benchmark on the rasterizer, native resolution ...
start "" /wait SolaceExpress.exe --raster --bench menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1,research20,ckv11_0_-10_12,ufo13_0 --size native --out "%OUT%\bench_raster_native.txt"
echo [2/5] Benchmark on the ray tracer, 1920x1080 (for comparison) ...
start "" /wait SolaceExpress.exe --bench menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1,research20,ckv11_0_-10_12,ufo13_0 --size 1920x1080 --out "%OUT%\bench_rt_1080p.txt"
echo [3/5] Analysis on the rasterizer (5-10 minutes) ...
start "" /wait SolaceExpress.exe --raster --analyze
if exist analysis.txt move /y analysis.txt "%OUT%\analysis_raster.txt" >nul
if exist analysis (
  if exist "%OUT%\analysis" rmdir /s /q "%OUT%\analysis"
  move /y analysis "%OUT%\analysis" >nul
)
echo [4/5] Screenshots on the rasterizer ...
set GAVOUT=1
set MTBAY=1
start "" /wait SolaceExpress.exe --raster --shots research10,research20,gav_11_120_10_0,gav_11_210_5_0,ckv11_0_-10_12,ckv11_-60_-20_12,ckv8_0_-10_12,gav_9_120_10_0,ufo13_0,ufo17_2,cockpit,rjetc,wr_8_0_0_0_1,hub1,menu --size 1920x1080
set GAVOUT=
set MTBAY=
if exist shots (
  if exist "%OUT%\shots" rmdir /s /q "%OUT%\shots"
  move /y shots "%OUT%\shots" >nul
)
echo [5/5] Logs and settings ...
if exist "%APPDATA%\SolaceExpress\startup.log" copy /y "%APPDATA%\SolaceExpress\startup.log" "%OUT%\startup.log" >nul
if exist "%APPDATA%\SolaceExpress\settings.cfg" copy /y "%APPDATA%\SolaceExpress\settings.cfg" "%OUT%\settings.cfg" >nul
echo version %VER% > "%OUT%\report_info.txt"
echo date %DATE% %TIME% >> "%OUT%\report_info.txt"
wmic path win32_VideoController get name,driverversion /format:list 2>nul | findstr /r /c:"=." >> "%OUT%\report_info.txt"
wmic cpu get name /format:list 2>nul | findstr /r /c:"=." >> "%OUT%\report_info.txt"
if exist "report_%VER%.zip" del /q "report_%VER%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath 'report_%VER%.zip' -Force"
echo.
echo ---------------------------------------------------------------
if exist "%OUT%\bench_raster_1080p.txt" type "%OUT%\bench_raster_1080p.txt"
echo ---------------------------------------------------------------
echo.
echo Done. Send over:  %~dp0report_%VER%.zip
echo (the "report" folder next to the exe holds the same files, unzipped)
start "" "%~dp0%OUT%"
pause
