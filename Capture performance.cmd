@echo off
setlocal
cd /d "%~dp0"
python tools\capture_performance.py
pause
