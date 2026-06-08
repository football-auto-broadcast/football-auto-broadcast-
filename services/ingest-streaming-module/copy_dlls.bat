@echo off
chcp 65001 >nul
echo ========================================
echo   DLL Copy Tool
echo ========================================
echo.

set "SOURCE_DIR=%~dp0"
set "OUTPUT_DIR=%SOURCE_DIR%x64\Release\"

echo Output directory: %OUTPUT_DIR%
echo.

if not exist "%OUTPUT_DIR%" (
    echo Creating output directory...
    mkdir "%OUTPUT_DIR%"
)

echo.
echo ========================================
echo   Step 1: Select GStreamer DLL Source
echo ========================================
echo.
echo Drag all GStreamer DLLs to this window, then press Enter
echo (from D:\C++ Big project\msvc_x86_64\bin\ select all .dll)
echo.
echo Or press Enter to skip and copy manually later
echo.
set /p GST_DLLS="Drop DLL files here: "
echo.

if not "%GST_DLLS%"=="" (
    echo Copying files...
    for %%f in (%GST_DLLS%) do (
        copy "%%f" "%OUTPUT_DIR%" /Y >nul
        echo Copied: %%~nxf
    )
)

echo.
echo ========================================
echo   Step 2: Copy GStreamer Plugins
echo ========================================
echo.

set "PLUGIN_SRC=%SOURCE_DIR%..\..\third_party\gstreamer\lib\gstreamer-1.0\"
set "PLUGIN_DST=%OUTPUT_DIR%lib\gstreamer-1.0\"

if exist "%PLUGIN_SRC%" (
    echo Copying plugins from %PLUGIN_SRC%...
    if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
    xcopy "%PLUGIN_SRC%\*.dll" "%PLUGIN_DST%\" /Y /Q >nul
    echo Plugins copied to: %PLUGIN_DST%
) else (
    echo [WARN] Cannot find plugin directory: %PLUGIN_SRC%
)

echo.
echo ========================================
echo   Step 3: Copy Config Files and Tools
echo ========================================
echo.

if exist "%SOURCE_DIR%bin\" (
    xcopy "%SOURCE_DIR%bin\*.yml" "%OUTPUT_DIR%\" /Y /Q >nul
    xcopy "%SOURCE_DIR%bin\*.exe" "%OUTPUT_DIR%\" /Y /Q >nul
    echo Config files and tools copied
)

echo.
echo ========================================
echo   Done!
echo ========================================
echo.
echo Next steps:
echo   1. Copy MvCameraControl.dll to %OUTPUT_DIR% manually
echo   2. Run the program in %OUTPUT_DIR%
echo   3. If needed, set: set GST_PLUGIN_PATH=%OUTPUT_DIR%lib\gstreamer-1.0
echo.
pause