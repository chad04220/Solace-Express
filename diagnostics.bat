@echo off
rem Solace Express diagnostics: one run that collects everything the performance work needs from your machine.
rem Put this file next to SolaceExpress.exe (the release zip's folder) and double-click it. It writes a "diagnostics"
rem folder and zips it as diagnostics_<version>.zip next to the exe: send that zip over. The full run takes 15-25
rem minutes; each window's title shows its progress. Close a window to skip that step.
rem
rem   diagnostics.bat           the full run (below)
rem   diagnostics.bat quick     the benchmarks and the system report only (about 3 minutes)
rem   diagnostics.bat shots     the screenshots only
rem   diagnostics.bat loading   renders the loading-screen pictures (the "loading" folder) on your GPU
rem
rem The full run:
rem   1. system report: GPU and driver, CPU, memory, Windows, monitors with their refresh rates
rem   2. first-run shader compile time (the shader cache is set aside), then the time to build every
rem      aircraft body from scratch with the shaders cached
rem   Diagnostics v2: every benchmark and screenshot run first builds (or loads) every aircraft body, outside and
rem   cockpit, the research craft's too, and each scene warms until nothing is being built or streamed before it is
rem   timed: no number includes a body being built or a traffic aircraft drawn the slow way for want of its body.
rem   3. benchmark, full screen at 1920x1080: frame time and
rem      the GPU time of every pass for each scene (the HUD, the cockpit, night); then the research craft and their
rem      cockpits in their own file (the heaviest scenes: if one stalls the GPU, the rest of the numbers are already written)
rem   4. the pass-by-pass analysis (CPU vs GPU, resolution scaling, what each feature costs)
rem   5. screenshots: the HUD with every warning lit, the research terminal and craft, the cockpits,
rem      the hangar, the menu
rem   6. startup.log (GPU, texture units, shader programs compiled, audio backend) and the settings file
cd /d "%~dp0"
if not exist SolaceExpress.exe (
  echo SolaceExpress.exe is not in this folder. Put diagnostics.bat next to it and run it again.
  pause
  exit /b 1
)
set VER=unknown
if exist VERSION.txt set /p VER=<VERSION.txt
set SCENES=menu,air,storm,night,cockpit,hud,hub1
set RSCENES=research10,research20,research40,rjet,wr_8_0_0_0_1,wr_8_0_-8_0_1_3_2_22.5_1,wr_8_0_-8_0_1_3_2_22.5_2,rjetc,ckv11_0_-10_12,ufo13_0
set MODE=%~1
if /i "%MODE%"=="loading" goto loading
set OUT=diagnostics
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if /i "%MODE%"=="shots" goto shots

echo [1/6] System report ...
set INFO=%OUT%\system.txt
echo Solace Express %VER%  (diagnostics v2) > "%INFO%"
echo date %DATE% %TIME% >> "%INFO%"
rem (the GPU's memory from its driver's registry entry: Win32_VideoController.AdapterRAM is 32 bits and reads 4095 MB
rem for any card with 4 GB or more)
powershell -NoProfile -Command ^
  "$o = @();" ^
  "$vram = @{}; Get-ItemProperty 'HKLM:\SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0*' -ErrorAction SilentlyContinue | ForEach-Object { $q = $_.'HardwareInformation.qwMemorySize'; if ($q -is [byte[]]) { $q = [BitConverter]::ToInt64($q, 0) }; if ($q -and $_.DriverDesc) { $vram[$_.DriverDesc] = [int64]$q } };" ^
  "$o += '--- GPU'; Get-CimInstance Win32_VideoController | ForEach-Object { $mb = if ($vram[$_.Name]) { [int64]($vram[$_.Name] / 1MB) } else { [int64]($_.AdapterRAM / 1MB) }; $o += ('  {0}  driver {1}  ({2} MB)' -f $_.Name, $_.DriverVersion, $mb) };" ^
  "$o += '--- CPU'; Get-CimInstance Win32_Processor | ForEach-Object { $o += ('  {0}  {1} cores / {2} threads  {3} MHz' -f $_.Name, $_.NumberOfCores, $_.NumberOfLogicalProcessors, $_.MaxClockSpeed) };" ^
  "$cs = Get-CimInstance Win32_ComputerSystem; $o += ('--- Memory  {0:N1} GB' -f ($cs.TotalPhysicalMemory / 1GB));" ^
  "$os = Get-CimInstance Win32_OperatingSystem; $o += ('--- Windows  {0}  build {1}' -f $os.Caption, $os.BuildNumber);" ^
  "$o += '--- Monitors'; Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { $o += ('  {0}  {1}x{2}{3}' -f $_.DeviceName, $_.Bounds.Width, $_.Bounds.Height, $(if ($_.Primary) { '  (primary)' } else { '' })) };" ^
  "Get-CimInstance Win32_VideoController | ForEach-Object { $o += ('  current mode {0}x{1} @ {2} Hz' -f $_.CurrentHorizontalResolution, $_.CurrentVerticalResolution, $_.CurrentRefreshRate) };" ^
  "$o += '--- Power'; $p = powercfg /getactivescheme; $o += ('  ' + $p);" ^
  "$o | Out-File -Append -Encoding utf8 '%INFO%'" 2>nul
type "%INFO%"

echo [2/6] First-run shader compile time, then every aircraft body built from scratch (the cache is set aside) ...
set CACHE=
if exist shadercache set CACHE=shadercache
if not defined CACHE if exist "%APPDATA%\SolaceExpress\shadercache" set CACHE=%APPDATA%\SolaceExpress\shadercache
if defined CACHE (
  if exist "%CACHE%.aside" rmdir /s /q "%CACHE%.aside"
  move /y "%CACHE%" "%CACHE%.aside" >nul 2>"%OUT%\cache_aside.txt"
  if exist "%CACHE%" echo The shader cache %CACHE% could not be set aside, so the first run below is not a cold compile.>>"%OUT%\cache_aside.txt"
)
if exist "%APPDATA%\SolaceExpress\startup.log" del /q "%APPDATA%\SolaceExpress\startup.log"
powershell -NoProfile -Command "$t = Measure-Command { Start-Process -FilePath 'SolaceExpress.exe' -ArgumentList '--bench menu --nobodies --size 1920x1080 --out %OUT%\compile_run.txt' -Wait }; $line = ('first run with an empty shader cache: {0:N1} s (compiles the shaders, then times the menu scene once; no aircraft bodies built)' -f $t.TotalSeconds); Write-Host $line; [IO.File]::WriteAllText('%OUT%\compile_time.txt', $line + [Environment]::NewLine)"
if exist "%APPDATA%\SolaceExpress\startup.log" copy /y "%APPDATA%\SolaceExpress\startup.log" "%OUT%\startup_firstrun.log" >nul
powershell -NoProfile -Command "$t = Measure-Command { Start-Process -FilePath 'SolaceExpress.exe' -ArgumentList '--bench menu --size 1920x1080 --out %OUT%\compile_run2.txt' -Wait }; $line = ('second run, shaders cached, every aircraft body built from scratch: {0:N1} s (the body build alone is in compile_run2.txt)' -f $t.TotalSeconds); Write-Host $line; [IO.File]::AppendAllText('%OUT%\compile_time.txt', $line + [Environment]::NewLine)"
if defined CACHE if exist "%CACHE%.aside" (
  rem the two runs built a fresh cache with this version's shaders and every aircraft body: it stays, the old one goes
  rem (it is put back only if the runs left no cache)
  if exist "%CACHE%" (rmdir /s /q "%CACHE%.aside") else (move /y "%CACHE%.aside" "%CACHE%" >nul)
)

echo [3/6] Benchmark, full screen 1920x1080 ...
start "" /wait SolaceExpress.exe --bench %SCENES% --size 1920x1080 --fullscreen --out %OUT%\bench_1080p.txt
if /i "%MODE%"=="quick" goto logs
echo [3/6] Benchmark, the research craft (their cockpits are the heaviest scenes: last, in their own file) ...
start "" /wait SolaceExpress.exe --bench %RSCENES% --size 1920x1080 --fullscreen --out %OUT%\bench_research_1080p.txt
echo [4/6] Analysis (5-10 minutes) ...
start "" /wait SolaceExpress.exe --analyze
if exist analysis.txt move /y analysis.txt "%OUT%\analysis.txt" >nul
if exist analysis (
  if exist "%OUT%\analysis" rmdir /s /q "%OUT%\analysis"
  move /y analysis "%OUT%\analysis" >nul
)

:shots
echo [5/6] Screenshots ...
set HUDDEMO=1
set GAVOUT=1
set MTBAY=1
start "" /wait SolaceExpress.exe --shots hud,research10,research20,research40,gav_11_120_10_0,gav_11_210_5_0,ckv11_0_-10_12,ckv11_-60_-20_12,ckv8_0_-10_12,gav_9_120_10_0,ufo13_0,ufo17_2,cockpit,rjetc,wr_8_0_0_0_1,wr_8_0_-8_0_1_3_2_22.5_1,wr_8_0_-8_0_1_3_2_22.5_2,hub1,hub2,menu --size 1920x1080
set HUDDEMO=
set GAVOUT=
set MTBAY=
if exist shots (
  if exist "%OUT%\shots" rmdir /s /q "%OUT%\shots"
  move /y shots "%OUT%\shots" >nul
)
if /i "%MODE%"=="shots" goto finish

:logs
echo [6/6] Logs and settings ...
if exist "%APPDATA%\SolaceExpress\startup.log" copy /y "%APPDATA%\SolaceExpress\startup.log" "%OUT%\startup.log" >nul
if exist "%APPDATA%\SolaceExpress\settings.cfg" copy /y "%APPDATA%\SolaceExpress\settings.cfg" "%OUT%\settings.cfg" >nul

:finish
if exist "diagnostics_%VER%.zip" del /q "diagnostics_%VER%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath 'diagnostics_%VER%.zip' -Force"
echo.
echo ---------------------------------------------------------------
if exist "%OUT%\compile_time.txt" type "%OUT%\compile_time.txt"
if exist "%OUT%\bench_1080p.txt" type "%OUT%\bench_1080p.txt"
echo ---------------------------------------------------------------
echo.
echo Done. Send over:  %~dp0diagnostics_%VER%.zip
echo (the "diagnostics" folder next to the exe holds the same files, unzipped)
rem (the folder is opened with Explorer by name: "start" on "...\diagnostics" tries the program extensions first, found
rem diagnostics.bat itself and ran the whole thing again, deleting this run's folder as it started)
explorer.exe "%~dp0%OUT%"
pause
exit /b 0

:loading
rem every airport and every aircraft in flight, saved in the "loading" folder; the pre-flight loading screen shows them
start "" /wait SolaceExpress.exe --loadshots
echo Done: the pictures are in the "loading" folder.
pause
exit /b 0
