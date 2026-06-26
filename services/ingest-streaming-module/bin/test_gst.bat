@echo off
cd /d "%~dp0"
echo ============================================
echo   GStreamer Basic Functionality Test
echo ============================================
echo.

set "GST_PLUGIN_PATH=%~dp0lib\gstreamer-1.0"
set "PATH=%~dp0;%PATH%"

echo GST_PLUGIN_PATH=%GST_PLUGIN_PATH%
echo.

if exist "%~dp0lib\gstreamer-1.0\gstapp.dll" (
    echo [OK] gstapp.dll found
) else (
    echo [FAIL] gstapp.dll NOT found
)

if exist "%~dp0lib\gstreamer-1.0\gstx264.dll" (
    echo [OK] gstx264.dll found
) else (
    echo [FAIL] gstx264.dll NOT found
)

if exist "%~dp0lib\gstreamer-1.0\gstvideoconvertscale.dll" (
    echo [OK] gstvideoconvertscale.dll found
) else (
    echo [FAIL] gstvideoconvertscale.dll NOT found
)

echo.
echo Test 1: list available elements (gstreamer-1.0-inspect)...
if exist "%~dp0gst-inspect-1.0.exe" (
    echo Found gst-inspect-1.0.exe
    echo.
    "%~dp0gst-inspect-1.0.exe" appsrc
    echo.
    echo appsrc element status: %ERRORLEVEL%
) else (
    echo [WARN] gst-inspect-1.0.exe not found
    echo.
)

echo.
echo Test 2: Create simple pipeline via program...
echo.

