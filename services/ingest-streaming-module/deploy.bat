@echo off
chcp 65001 >nul
echo ========================================
echo   采集与编码分发模块 - 部署脚本
echo ========================================
echo.

set "SOURCE_DIR=%~dp0"
set "PROJECT_ROOT=%SOURCE_DIR%..\..\"
set "THIRD_PARTY=%PROJECT_ROOT%third_party\"
set "OUTPUT_DIR=%SOURCE_DIR%x64\Release\"

echo [1/4] 检查输出目录...
if not exist "%OUTPUT_DIR%" (
    echo 创建输出目录: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
)
echo 输出目录: %OUTPUT_DIR%
echo.

echo [2/4] 复制 GStreamer DLL (从 third_party)...
if not exist "%THIRD_PARTY%gstreamer\bin\" (
    echo [ERROR] 找不到 GStreamer DLL 目录: %THIRD_PARTY%gstreamer\bin\
    echo 请确认 third_party\gstreamer\bin\ 目录存在！
    pause
    exit /b 1
)

echo 从 %THIRD_PARTY%gstreamer\bin\ 复制 DLL 到 %OUTPUT_DIR%
xcopy "%THIRD_PARTY%gstreamer\bin\*.dll" "%OUTPUT_DIR%" /Y /Q >nul
echo 已复制 GStreamer DLL
echo.

echo [3/4] 复制 GStreamer 插件...
if not exist "%OUTPUT_DIR%lib\gstreamer-1.0\" (
    mkdir "%OUTPUT_DIR%lib\gstreamer-1.0\"
)
xcopy "%THIRD_PARTY%gstreamer\lib\gstreamer-1.0\*.dll" "%OUTPUT_DIR%lib\gstreamer-1.0\" /Y /Q >nul
echo 已复制 GStreamer 插件
echo.

echo [4/4] 复制 MVS SDK DLL...
if exist "%THIRD_PARTY%mvs_sdk\win64\" (
    xcopy "%THIRD_PARTY%mvs_sdk\win64\*.dll" "%OUTPUT_DIR%" /Y /Q >nul
    xcopy "%THIRD_PARTY%mvs_sdk\win64\*.lib" "%OUTPUT_DIR%" /Y /Q >nul
    echo 已复制 MVS SDK DLL
) else (
    echo [WARN] 找不到 MVS SDK DLL: %THIRD_PARTY%mvs_sdk\win64\
    echo 请确保已安装 MVS SDK
)

echo [5/5] 复制配置文件和工具...
if exist "%SOURCE_DIR%bin\" (
    xcopy "%SOURCE_DIR%bin\*.yml" "%OUTPUT_DIR%" /Y /Q >nul
    xcopy "%SOURCE_DIR%bin\*.exe" "%OUTPUT_DIR%" /Y /Q >nul
    echo 已复制配置文件和工具
)
echo.

echo ========================================
echo   部署完成！
echo ========================================
echo.
echo 在 %OUTPUT_DIR% 中运行程序即可
echo.
pause
