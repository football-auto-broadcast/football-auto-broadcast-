@echo off
chcp 65001 >nul
echo ========================================
echo   Deployment Script for Ingest Module
echo ========================================
echo.

set "SOURCE_DIR=%~dp0"
set "PROJECT_ROOT=%SOURCE_DIR%..\..\"
set "THIRD_PARTY=%PROJECT_ROOT%third_party\"
set "OUTPUT_DIR=%SOURCE_DIR%x64\Release\"

echo [1/4] Checking output directory...
if not exist "%OUTPUT_DIR%" (
    echo Creating output directory: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
)
echo Output directory: %OUTPUT_DIR%
echo.

echo [2/4] Copying GStreamer DLLs...
if not exist "%THIRD_PARTY%gstreamer\bin\" (
    echo [ERROR] Cannot find GStreamer DLL directory: %THIRD_PARTY%gstreamer\bin\
    echo Please check if third_party\gstreamer\bin\ exists!
    pause
    exit /b 1
)

echo Copying DLLs from %THIRD_PARTY%gstreamer\bin\ to %OUTPUT_DIR%
xcopy "%THIRD_PARTY%gstreamer\bin\*.dll" "%OUTPUT_DIR%" /Y /Q >nul
echo Copied GStreamer DLLs
echo.

echo [3/4] Copying GStreamer plugins...
if not exist "%OUTPUT_DIR%lib\gstreamer-1.0\" (
    mkdir "%OUTPUT_DIR%lib\gstreamer-1.0\"
)
xcopy "%THIRD_PARTY%gstreamer\lib\gstreamer-1.0\*.dll" "%OUTPUT_DIR%lib\gstreamer-1.0\" /Y /Q >nul
echo Copied GStreamer plugins
echo.

echo [4/4] Copying MVS SDK DLLs...
if exist "%THIRD_PARTY%mvs_sdk\win64\" (
    xcopy "%THIRD_PARTY%mvs_sdk\win64\*.dll" "%OUTPUT_DIR%" /Y /Q >nul
    xcopy "%THIRD_PARTY%mvs_sdk\win64\*.lib" "%OUTPUT_DIR%" /Y /Q >nul
    echo Copied MVS SDK DLLs
) else (
    echo [WARN] Cannot find MVS SDK: %THIRD_PARTY%mvs_sdk\win64\
    echo Please install MVS SDK
)

echo [5/5] Copying config files and tools...
if exist "%SOURCE_DIR%bin\" (
    xcopy "%SOURCE_DIR%bin\mediamtx.exe" "%OUTPUT_DIR%" /Y /Q >nul
    xcopy "%SOURCE_DIR%bin\mediamtx2.yml" "%OUTPUT_DIR%\mediamtx.yml" /Y /Q >nul
    echo Copied config files and tools
)
echo.

echo [6/6] Copying FFmpeg tools...
if exist "%THIRD_PARTY%ffmpeg\bin\" (
    xcopy "%THIRD_PARTY%ffmpeg\bin\*.exe" "%OUTPUT_DIR%" /Y /Q >nul
    echo Copied FFmpeg tools
) else (
    echo [WARN] Cannot find FFmpeg: %THIRD_PARTY%ffmpeg\bin\
)

echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo Run program in %OUTPUT_DIR%
echo.
pause