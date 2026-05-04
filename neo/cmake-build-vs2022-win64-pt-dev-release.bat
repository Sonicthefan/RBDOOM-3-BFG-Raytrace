@echo off
cd /d "%~dp0"
cmake --build --preset win64-pt-dev-release
pause
