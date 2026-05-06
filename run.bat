@echo off
if "%~1"=="" (
    echo 用法: run.bat ^<source.c^>
    exit /b 1
)
cmake --build build --config Debug && build\Debug\minic.exe %~1
