@echo off
cd /d "%~dp0"
echo ============================================
echo   Ingest Launcher (Camera to GStreamer to RTSP)
echo ============================================
echo.

set "GST_PLUGIN_PATH=%~dp0lib\gstreamer-1.0"
set "PATH=%~dp0;%PATH%"

echo GST_PLUGIN_PATH=%GST_PLUGIN_PATH%
echo.
echo Launching ingest-streaming-module.exe ...
echo.

ingest-streaming-module.exe

echo.
echo Exit code: %ERRORLEVEL%
pause
