@echo off
rem Solace Express performance analysis: renders eight scenes at 1920x1080 and measures CPU vs GPU time, the exact GPU
rem time of every pass, frame time against render resolution, what each ray-tracing feature costs, and how much work
rem the ray tracer does per pixel (with heat maps). Writes analysis.txt and the "analysis" folder next to
rem SolaceExpress.exe. Takes 5-10 minutes; the window title shows progress.
cd /d "%~dp0"
start "" /wait SolaceExpress.exe --analyze
type analysis.txt
pause
