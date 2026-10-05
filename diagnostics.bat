@echo off
rem Solace Express diagnostics: one run that collects everything the performance work needs from your machine.
rem Put this file next to SolaceExpress.exe (the release zip's folder) and double-click it. It writes a "diagnostics"
rem folder and zips it as diagnostics_<version>.zip next to the exe: send that zip over. The full run takes 15-25
rem minutes; each window's title shows its progress. Close a window to skip that step.
rem
rem   diagnostics.bat           the full run (below)
rem   diagnostics.bat quick     the benchmarks and the system report only (about 3 minutes)
rem   diagnostics.bat shots     the screenshots only
rem   diagnostics.bat menu      renders the main-menu video (menu.mp4) on your GPU; add a bit rate in kbit/s (default 5000)
rem   diagnostics.bat loading   renders the loading-screen pictures (the "loading" folder) on your GPU
rem
rem The full run:
rem   1. system report: GPU and driver, CPU, memory, Windows, monitors with their refresh rates
rem   2. first-run shader compile time on the rasterizer (the shader cache is set aside and restored afterwards)
rem   3. benchmark on the rasterizer (the default renderer) at 1920x1080 and at your native resolution: frame time and
rem      the GPU time of every pass for each scene, including the new HUD, the research craft, the cockpits and the UFO
rem   4. the same benchmark on the ray tracer at 1920x1080, for comparison
rem   5. the pass-by-pass analysis on the rasterizer (CPU vs GPU, resolution scaling, per-pixel work with heat maps)
rem   6. screenshots on the rasterizer: the HUD with every warning lit, the research terminal and craft, the cockpits,
rem      the hangar, the menu
rem   7. startup.log (GPU, texture units, shader programs compiled, audio backend) and the settings file
cd /d "%~dp0"
if not exist SolaceExpress.exe (
  echo SolaceExpress.exe is not in this folder. Put diagnostics.bat next to it and run it again.
  pause
  exit /b 1
)
set VER=unknown
if exist VERSION.txt set /p VER=<VERSION.txt
set SCENES=menu,air,storm,night,cockpit,hud,rjetc,wr_8_0_0_0_1,research10,research20,research40,ckv11_0_-10_12,ufo13_0,hub1
set MODE=%~1
if /i "%MODE%"=="menu" goto menu
if /i "%MODE%"=="loading" goto loading
set OUT=diagnostics
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if /i "%MODE%"=="shots" goto shots

echo [1/7] System report ...
set INFO=%OUT%\system.txt
echo Solace Express %VER% > "%INFO%"
echo date %DATE% %TIME% >> "%INFO%"
powershell -NoProfile -Command ^
  "$o = @();" ^
  "$o += '--- GPU'; Get-CimInstance Win32_VideoController | ForEach-Object { $o += ('  {0}  driver {1}  ({2} MB)' -f $_.Name, $_.DriverVersion, [int]($_.AdapterRAM / 1MB)) };" ^
  "$o += '--- CPU'; Get-CimInstance Win32_Processor | ForEach-Object { $o += ('  {0}  {1} cores / {2} threads  {3} MHz' -f $_.Name, $_.NumberOfCores, $_.NumberOfLogicalProcessors, $_.MaxClockSpeed) };" ^
  "$cs = Get-CimInstance Win32_ComputerSystem; $o += ('--- Memory  {0:N1} GB' -f ($cs.TotalPhysicalMemory / 1GB));" ^
  "$os = Get-CimInstance Win32_OperatingSystem; $o += ('--- Windows  {0}  build {1}' -f $os.Caption, $os.BuildNumber);" ^
  "$o += '--- Monitors'; Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { $o += ('  {0}  {1}x{2}{3}' -f $_.DeviceName, $_.Bounds.Width, $_.Bounds.Height, $(if ($_.Primary) { '  (primary)' } else { '' })) };" ^
  "Get-CimInstance Win32_VideoController | ForEach-Object { $o += ('  current mode {0}x{1} @ {2} Hz' -f $_.CurrentHorizontalResolution, $_.CurrentVerticalResolution, $_.CurrentRefreshRate) };" ^
  "$o += '--- Power'; $p = powercfg /getactivescheme; $o += ('  ' + $p);" ^
  "$o | Out-File -Append -Encoding utf8 '%INFO%'" 2>nul
type "%INFO%"

echo [2/7] First-run shader compile time on the rasterizer (the cache is set aside) ...
set CACHE=
if exist shadercache set CACHE=shadercache
if not defined CACHE if exist "%APPDATA%\SolaceExpress\shadercache" set CACHE=%APPDATA%\SolaceExpress\shadercache
if defined CACHE (
  if exist "%CACHE%.aside" rmdir /s /q "%CACHE%.aside"
  move /y "%CACHE%" "%CACHE%.aside" >nul
)
if exist "%APPDATA%\SolaceExpress\startup.log" del /q "%APPDATA%\SolaceExpress\startup.log"
powershell -NoProfile -Command "$t = Measure-Command { Start-Process -FilePath 'SolaceExpress.exe' -ArgumentList '--raster --bench menu --size 1920x1080 --out \"%OUT%\compile_run.txt\"' -Wait }; ('first run with an empty shader cache: {0:N1} s (compiles the shaders, then times the menu scene once)' -f $t.TotalSeconds) | Tee-Object -FilePath '%OUT%\compile_time.txt'"
if exist "%APPDATA%\SolaceExpress\startup.log" copy /y "%APPDATA%\SolaceExpress\startup.log" "%OUT%\startup_firstrun.log" >nul
powershell -NoProfile -Command "$t = Measure-Command { Start-Process -FilePath 'SolaceExpress.exe' -ArgumentList '--raster --bench menu --size 1920x1080 --out \"%OUT%\compile_run2.txt\"' -Wait }; ('second run with the cache warm: {0:N1} s' -f $t.TotalSeconds) | Tee-Object -Append -FilePath '%OUT%\compile_time.txt'"
if defined CACHE (
  if exist "%CACHE%" rmdir /s /q "%CACHE%.aside"
  if not exist "%CACHE%" move /y "%CACHE%.aside" "%CACHE%" >nul
)

echo [3/7] Benchmark on the rasterizer, 1920x1080 ...
start "" /wait SolaceExpress.exe --raster --bench %SCENES% --size 1920x1080 --out "%OUT%\bench_raster_1080p.txt"
echo [3/7] Benchmark on the rasterizer, native resolution ...
start "" /wait SolaceExpress.exe --raster --bench %SCENES% --size native --out "%OUT%\bench_raster_native.txt"
if /i "%MODE%"=="quick" goto logs
echo [4/7] Benchmark on the ray tracer, 1920x1080 (for comparison) ...
start "" /wait SolaceExpress.exe --bench %SCENES% --size 1920x1080 --out "%OUT%\bench_rt_1080p.txt"
echo [5/7] Analysis on the rasterizer (5-10 minutes) ...
start "" /wait SolaceExpress.exe --raster --analyze
if exist analysis.txt move /y analysis.txt "%OUT%\analysis_raster.txt" >nul
if exist analysis (
  if exist "%OUT%\analysis" rmdir /s /q "%OUT%\analysis"
  move /y analysis "%OUT%\analysis" >nul
)

:shots
echo [6/7] Screenshots on the rasterizer ...
set HUDDEMO=1
set GAVOUT=1
set MTBAY=1
start "" /wait SolaceExpress.exe --raster --shots hud,research10,research20,research40,gav_11_120_10_0,gav_11_210_5_0,ckv11_0_-10_12,ckv11_-60_-20_12,ckv8_0_-10_12,gav_9_120_10_0,ufo13_0,ufo17_2,cockpit,rjetc,wr_8_0_0_0_1,hub1,hub2,menu --size 1920x1080
set HUDDEMO=
set GAVOUT=
set MTBAY=
if exist shots (
  if exist "%OUT%\shots" rmdir /s /q "%OUT%\shots"
  move /y shots "%OUT%\shots" >nul
)
if /i "%MODE%"=="shots" goto finish

:logs
echo [7/7] Logs and settings ...
if exist "%APPDATA%\SolaceExpress\startup.log" copy /y "%APPDATA%\SolaceExpress\startup.log" "%OUT%\startup.log" >nul
if exist "%APPDATA%\SolaceExpress\settings.cfg" copy /y "%APPDATA%\SolaceExpress\settings.cfg" "%OUT%\settings.cfg" >nul

:finish
if exist "diagnostics_%VER%.zip" del /q "diagnostics_%VER%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath 'diagnostics_%VER%.zip' -Force"
echo.
echo ---------------------------------------------------------------
if exist "%OUT%\compile_time.txt" type "%OUT%\compile_time.txt"
if exist "%OUT%\bench_raster_1080p.txt" type "%OUT%\bench_raster_1080p.txt"
echo ---------------------------------------------------------------
echo.
echo Done. Send over:  %~dp0diagnostics_%VER%.zip
echo (the "diagnostics" folder next to the exe holds the same files, unzipped)
start "" "%~dp0%OUT%"
pause
exit /b 0

:menu
rem the main-menu montage (8 shots, a 128 second loop) rendered at 1920x1080 and 30 fps with its scenery complete, saved
rem as menu.mp4 next to the exe; the menu then plays the video instead of rendering the montage live
if "%~2"=="" (start "" /wait SolaceExpress.exe --menuvideo) else (start "" /wait SolaceExpress.exe --menuvideo --kbps %~2)
if exist menu.mp4 (echo Done: menu.mp4 is next to SolaceExpress.exe.) else (echo The menu video was not made.)
pause
exit /b 0

:loading
rem every airport and every aircraft in flight, saved in the "loading" folder; the pre-flight loading screen shows them
start "" /wait SolaceExpress.exe --loadshots
echo Done: the pictures are in the "loading" folder.
pause
exit /b 0
