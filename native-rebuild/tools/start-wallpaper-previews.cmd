@echo off
powershell.exe -NoProfile -File "%~dp0make-wallpaper-previews-batch.ps1" %*
if errorlevel 1 (echo Preview generation failed. Re-run to resume.) else (echo Preview batch is ready.)
pause
