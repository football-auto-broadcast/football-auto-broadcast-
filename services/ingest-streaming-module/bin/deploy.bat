@echo off
chcp 65001 >nul
echo ========================================
echo   部署依赖文件到 bin 目录
echo ========================================
echo.

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\..\..\..\"
set "GSTREAMER_DIR=%PROJECT_ROOT%third_party\gstreamer\"
set "MVS_DIR=%PROJECT_ROOT%third_party\mvs_sdk\win64\"

cd /d "%SCRIPT_DIR%"

echo 目标目录: %SCRIPT_DIR%
echo.
echo 源目录:
echo   GStreamer: %GSTREAMER_DIR%
echo   MVS SDK:   %MVS_DIR%
echo.

echo [1/3] 复制 GStreamer DLL...
if not exist "%GSTREAMER_DIR%bin\" (
    echo [ERROR] 找不到 GStreamer bin 目录: %GSTREAMER_DIR%bin\
    pause
    exit /b 1
)

echo   正在复制 ~150 个 DLL 文件...
xcopy "%GSTREAMER_DIR%bin\*.dll" "%SCRIPT_DIR%" /Y /Q >nul
echo   [OK] GStreamer DLL 复制完成
echo.

echo [2/3] 复制 GStreamer 插件...
if not exist "%GSTREAMER_DIR%lib\gstreamer-1.0\" (
    echo [ERROR] 找不到 GStreamer 插件目录: %GSTREAMER_DIR%lib\gstreamer-1.0\
    pause
    exit /b 1
)

if not exist "%SCRIPT_DIR%lib\gstreamer-1.0\" (
    mkdir "%SCRIPT_DIR%lib\gstreamer-1.0\"
)

echo   正在复制 ~200 个插件 DLL...
xcopy "%GSTREAMER_DIR%lib\gstreamer-1.0\*.dll" "%SCRIPT_DIR%lib\gstreamer-1.0\" /Y /Q >nul
echo   [OK] GStreamer 插件复制完成
echo.

echo [3/3] 复制 MVS SDK DLL (相机驱动)...
if exist "%MVS_DIR%" (
    echo   正在复制 MVS SDK DLL...
    xcopy "%MVS_DIR%*.dll" "%SCRIPT_DIR%" /Y /Q >nul 2>nul
    echo   [OK] MVS SDK DLL 复制完成
) else (
    echo   [WARN] 未找到 MVS SDK 目录: %MVS_DIR%
    echo   如果已安装海康威视 MVS 客户端, 相关 DLL 通常在 PATH 中
    echo   可以跳过此步骤
)
echo.

echo ========================================
echo   部署完成!
echo ========================================
echo.
echo 目录内容检查:
dir "%SCRIPT_DIR%*.dll" /b 2>nul | find /c /v "" > temp_count.txt
set /p DLL_COUNT=<temp_count.txt
del temp_count.txt
echo   DLL 文件数量: %DLL_COUNT%

if exist "%SCRIPT_DIR%lib\gstreamer-1.0\" (
    dir "%SCRIPT_DIR%lib\gstreamer-1.0\*.dll" /b 2>nul | find /c /v "" > temp_count.txt
    set /p PLUGIN_COUNT=<temp_count.txt
    del temp_count.txt
    echo   GStreamer 插件数量: %PLUGIN_COUNT%
)
echo.
echo 下一步:
echo   1. 运行 start.bat 启动所有服务
echo   2. 使用 ffplay 播放 rtsp://127.0.0.1:8554/main
echo.
pause
