@echo off
rem Solace Express benchmark: times several scenes on your GPU (frame time and the GPU time of every rendering pass)
rem and writes bench_1080p.txt and bench_native.txt next to SolaceExpress.exe. Takes about a minute.
cd /d "%~dp0"
start "" /wait SolaceExpress.exe --bench menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1 --size 1920x1080 --out bench_1080p.txt
start "" /wait SolaceExpress.exe --bench menu,air,storm,night,cockpit,rjetc,wr_8_0_0_0_1 --size native --out bench_native.txt
echo.
type bench_1080p.txt
echo.
type bench_native.txt
echo.
echo Results saved in bench_1080p.txt and bench_native.txt (send them over).
pause
