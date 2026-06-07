@echo off
chcp 65001 >nul
echo ========================================
echo   DLL 一键复制工具
echo ========================================
echo.

set "SOURCE_DIR=%~dp0"
set "OUTPUT_DIR=%SOURCE_DIR%x64\Release\"

echo 输出目录: %OUTPUT_DIR%
echo.

if not exist "%OUTPUT_DIR%" (
    echo 创建输出目录...
    mkdir "%OUTPUT_DIR%"
)

echo.
echo ========================================
echo   步骤 1: 选择 GStreamer DLL 来源
echo ========================================
echo.
echo 请将所有 GStreamer DLL 拖放到此窗口，然后按回车
echo (从 D:\C++ Big project\msvc_x86_64\bin\ 选择所有 .dll)
echo.
echo 或者直接回车跳过，稍后手动复制
echo.
set /p GST_DLLS="拖放 DLL 文件: "
echo.

if not "%GST_DLLS%"=="" (
    echo 正在复制...
    for %%f in (%GST_DLLS%) do (
        copy "%%f" "%OUTPUT_DIR%" /Y >nul
        echo 已复制: %%~nxf
    )
)

echo.
echo ========================================
echo   步骤 2: 复制 GStreamer 插件
echo ========================================
echo.

set "PLUGIN_SRC=%SOURCE_DIR%..\..\third_party\gstreamer\lib\gstreamer-1.0\"
set "PLUGIN_DST=%OUTPUT_DIR%lib\gstreamer-1.0\"

if exist "%PLUGIN_SRC%" (
    echo 从 %PLUGIN_SRC% 复制插件...
    if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
    xcopy "%PLUGIN_SRC%\*.dll" "%PLUGIN_DST%\" /Y /Q >nul
    echo 插件已复制到: %PLUGIN_DST%
) else (
    echo [WARN] 找不到插件目录: %PLUGIN_SRC%
)

echo.
echo ========================================
echo   步骤 3: 复制配置文件和工具
echo ========================================
echo.

if exist "%SOURCE_DIR%bin\" (
    xcopy "%SOURCE_DIR%bin\*.yml" "%OUTPUT_DIR%\" /Y /Q >nul
    xcopy "%SOURCE_DIR%bin\*.exe" "%OUTPUT_DIR%\" /Y /Q >nul
    echo 配置和工具已复制
)

echo.
echo ========================================
echo   完成！
echo ========================================
echo.
echo 接下来请:
echo   1. 手动复制 MvCameraControl.dll 到 %OUTPUT_DIR%
echo   2. 在 %OUTPUT_DIR% 中运行程序
echo   3. 如需要，设置: set GST_PLUGIN_PATH=%OUTPUT_DIR%lib\gstreamer-1.0
echo.
pause
