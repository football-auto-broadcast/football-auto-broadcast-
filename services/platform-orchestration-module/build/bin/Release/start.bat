@echo off
chcp 65001 >nul
title 足球转播平台 - 模块E

echo ========================================
echo   足球赛事自动转播系统
echo   平台与调度模块 (E) v1.2
echo ========================================
echo.
echo 启动中... 浏览器访问 http://localhost:8080
echo 按 Ctrl+C 退出
echo.
start http://localhost:8080
platform_orchestration_service.exe
pause
