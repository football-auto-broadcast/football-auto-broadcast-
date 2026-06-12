@echo off
chcp 65001 >nul
echo ========================================
echo   双实例启动脚本 (Ingest Streaming)
echo ========================================
echo.

cd /d "%~dp0"

echo [1/3] 检查依赖文件...
if not exist "mediamtx.exe" (
    echo [ERROR] 找不到 mediamtx.exe
    pause
    exit /b 1
)

if not exist "ingest-streaming-module.exe" (
    echo [ERROR] 找不到 ingest-streaming-module.exe
    echo 请先编译项目(Release + x64)后复制exe到bin目录
    pause
    exit /b 1
)

if not exist "ffmpeg.exe" (
    echo [ERROR] 找不到 ffmpeg.exe
    pause
    exit /b 1
)

if not exist "mediamtx_8554.yml" (
    echo [ERROR] 找不到 mediamtx_8554.yml (主相机配置)
    pause
    exit /b 1
)

if not exist "mediamtx_8555.yml" (
    echo [ERROR] 找不到 mediamtx_8555.yml (辅相机配置)
    pause
    exit /b 1
)

if not exist "lib\gstreamer-1.0\" (
    echo [WARN] 未检测到 GStreamer 插件目录 (lib\gstreamer-1.0\)
    echo 请先运行 deploy.bat 复制依赖
)

echo 所有必要文件已就绪!
echo.

echo [2/3] 启动双实例 MediaMTX 服务器...
echo   - 实例 1 (主相机): RTSP :8554, 路径 /main
echo   - 实例 2 (辅相机): RTSP :8555, 路径 /aux
echo.

start "MediaMTX-8554 (main)" mediamtx.exe mediamtx_8554.yml
timeout /t 2 /nobreak >nul

start "MediaMTX-8555 (aux)" mediamtx.exe mediamtx_8555.yml
timeout /t 2 /nobreak >nul

echo [3/3] 启动 Ingest 采集模块...
echo   - 连接两台 GigE 相机 (F92514845, D92514830)
echo   - 通过 GStreamer 编码为 H.264
echo   - 推送到各自的 MediaMTX 实例
echo.

start "Ingest Module" ingest-streaming-module.exe

echo.
echo ========================================
echo   所有服务已启动!
echo ========================================
echo.
echo 推流地址:
echo   - 主相机: rtsp://127.0.0.1:8554/main
echo   - 辅相机: rtsp://127.0.0.1:8555/aux
echo.
echo HLS 地址 (可选):
echo   - 主相机: http://127.0.0.1:8888/main
echo   - 辅相机: http://127.0.0.1:8889/aux
echo.
echo 播放命令:
echo   ffplay rtsp://127.0.0.1:8554/main
echo   ffplay rtsp://127.0.0.1:8555/aux
echo.
echo 状态监控:
echo   HTTP 接口: http://127.0.0.1:8081/api/v1/ingest/status
echo.
echo 提示: 关闭此窗口不会停止服务
echo.
pause
