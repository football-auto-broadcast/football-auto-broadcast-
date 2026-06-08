@echo off
chcp 65001 >nul
echo ========================================
echo   Quick Start for Ingest Module
echo ========================================
echo.

cd /d "%~dp0"

echo [1/2] Checking dependencies...
if not exist "mediamtx.exe" (
    echo [ERROR] Cannot find mediamtx.exe
    echo Please run deploy.bat first
    pause
    exit /b 1
)

if not exist "ingest-streaming-module.exe" (
    echo [ERROR] Cannot find ingest-streaming-module.exe
    echo Please build the project first (Release + x64)
    pause
    exit /b 1
)

echo Dependencies OK
echo.

echo [2/2] Starting services...
echo Starting MediaMTX (RTSP Server)...
start "MediaMTX" mediamtx.exe mediamtx.yml

timeout /t 2 /nobreak >nul

echo Starting Ingest Module...
start "Ingest Module" ingest-streaming-module.exe

echo.
echo ========================================
echo   Services Started!
echo ========================================
echo.
echo RTSP Stream URLs:
echo   Main Camera: rtsp://127.0.0.1:8554/main
echo   Aux Camera: rtsp://127.0.0.1:8554/aux
echo.
echo HTTP Status API:
echo   http://127.0.0.1:8081/api/v1/ingest/status
echo.
echo Press Ctrl+C to close this window (services continue running)
echo.
pause