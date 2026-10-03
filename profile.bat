@echo off
rem Solace Express profiler: renders a few scenes at 1920x1080 with each ray-tracing feature switched off in turn
rem and writes profile.txt next to SolaceExpress.exe (what each feature costs on your GPU). Takes about 5 minutes.
cd /d "%~dp0"
start "" /wait SolaceExpress.exe --profile menu,air,cockpit,rjetc,wr_8_0_0_0_1
type profile.txt
pause
