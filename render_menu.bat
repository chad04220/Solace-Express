@echo off
rem Solace Express main-menu video: renders the whole main-menu montage (8 shots, a 128 second loop) on your GPU
rem and saves it as menu.mp4 next to SolaceExpress.exe. The main menu then plays this video instead of ray tracing
rem the montage live, so the menu runs at full frame rate. Every frame is rendered with its scenery complete at
rem 1920x1080 and 30 fps, so this takes a while (the window title shows the progress). Close the window to stop.
rem Optional: render_menu.bat 8000   sets the video bit rate in kbit/s (default 5000, about 80 MB).
cd /d "%~dp0"
if "%~1"=="" (start "" /wait SolaceExpress.exe --menuvideo) else (start "" /wait SolaceExpress.exe --menuvideo --kbps %~1)
if exist menu.mp4 (echo Done: menu.mp4 is next to SolaceExpress.exe.) else (echo The menu video was not made.)
pause
