@echo off
chcp 65001 >nul
echo ========================================
echo   本地视频推流测试 (无相机)
echo ========================================
echo.

cd /d "%~dp0"

echo [1/3] 检查文件...
if not exist "mediamtx.exe" (echo [ERROR] 缺少 mediamtx.exe & pause & exit /b 1)
if not exist "ffmpeg.exe" (echo [ERROR] 缺少 ffmpeg.exe & pause & exit /b 1)
if not exist "mediamtx_8554.yml" (echo [ERROR] 缺少 mediamtx_8554.yml & pause & exit /b 1)
if not exist "mediamtx_8555.yml" (echo [ERROR] 缺少 mediamtx_8555.yml & pause & exit /b 1)
if not exist "test.mp4" (echo [ERROR] 缺少 test.mp4 & pause & exit /b 1)

echo   [OK] 所有文件存在
echo.

echo [2/3] 启动双实例 MediaMTX 服务器...
start "MediaMTX-8554 (main)" mediamtx.exe mediamtx_8554.yml
timeout /t 2 /nobreak >nul
start "MediaMTX-8555 (aux)" mediamtx.exe mediamtx_8555.yml
timeout /t 2 /nobreak >nul
echo   [OK] 服务器已启动
echo.

echo [3/3] 开始推送本地测试视频...
echo   注意: test.mp4 会循环播放 (stream_loop -1)
echo   如果 test.mp4 是 HEVC 编码, 将自动转码为 H.264
echo.

start "FFmpeg-main" cmd /c "ffmpeg.exe -re -stream_loop -1 -i test.mp4 -c:v libx264 -preset fast -b:v 2000k -c:a aac -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/main"
timeout /t 1 /nobreak >nul
start "FFmpeg-aux" cmd /c "ffmpeg.exe -re -stream_loop -1 -i test.mp4 -c:v libx264 -preset fast -b:v 2000k -c:a aac -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8555/aux"

echo.
echo ========================================
echo   推流已启动!
echo ========================================
echo.
echo 播放测试:
echo   ffplay rtsp://127.0.0.1:8554/main
echo   ffplay rtsp://127.0.0.1:8555/aux
echo.
echo 停止服务:
echo   关闭所有 cmd 窗口 (MediaMTX-8554, MediaMTX-8555, FFmpeg-main, FFmpeg-aux)
echo.
pause
