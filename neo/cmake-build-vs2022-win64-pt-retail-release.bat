@echo off
cd /d "%~dp0"
cmake --build --preset win64-pt-retail-release
pause
