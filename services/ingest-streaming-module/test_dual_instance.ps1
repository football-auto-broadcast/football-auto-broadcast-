<#
.SYNOPSIS
双实例测试脚本（本地视频）

.DESCRIPTION
启动两个 MediaMTX 实例并推送测试视频，使用项目内的 FFmpeg
#>

$ErrorActionPreference = "Stop"
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptPath

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Dual Instance Test Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. 检查测试视频
Write-Host "[1/4] Checking test video..." -ForegroundColor Yellow
$testVideo = Join-Path $scriptPath "bin\test.mp4"
if (-not (Test-Path $testVideo)) {
    Write-Host "[ERROR] Cannot find test video: $testVideo" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}
Write-Host "Test video: $testVideo" -ForegroundColor Green
Write-Host ""

# 2. 检查本地 FFmpeg
Write-Host "[2/4] Checking FFmpeg..." -ForegroundColor Yellow
$ffmpegExe = Join-Path $scriptPath "..\..\third_party\ffmpeg\bin\ffmpeg.exe"
if (-not (Test-Path $ffmpegExe)) {
    Write-Host "[ERROR] Cannot find FFmpeg: $ffmpegExe" -ForegroundColor Red
    Write-Host "Please place FFmpeg in third_party/ffmpeg/bin/" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}
Write-Host "FFmpeg found: $ffmpegExe" -ForegroundColor Green
Write-Host ""

# 3. 清理旧进程
Write-Host "[3/4] Cleaning up old processes..." -ForegroundColor Yellow
Stop-Process -Name mediamtx -Force -ErrorAction SilentlyContinue
Stop-Process -Name ffmpeg -Force -ErrorAction SilentlyContinue
Write-Host "Cleanup done" -ForegroundColor Green
Write-Host ""

# 4. 启动两个 MediaMTX 实例
Write-Host "[4/4] Starting two MediaMTX instances..." -ForegroundColor Yellow

Write-Host "Instance 1: Port 8554 (main path)" -ForegroundColor White
$mediamtxExe = Join-Path $scriptPath "bin\mediamtx.exe"
$config1 = Join-Path $scriptPath "bin\mediamtx_8554.yml"
Start-Process -FilePath $mediamtxExe -ArgumentList "`"$config1`"" -PassThru | Out-Null

Start-Sleep -Seconds 2

Write-Host "Instance 2: Port 8555 (aux path)" -ForegroundColor White
$config2 = Join-Path $scriptPath "bin\mediamtx_8555.yml"
Start-Process -FilePath $mediamtxExe -ArgumentList "`"$config2`"" -PassThru | Out-Null

Start-Sleep -Seconds 2

# 5. 推送测试视频到两个实例（使用本地 FFmpeg）
Write-Host ""
Write-Host "Pushing test video to both instances..." -ForegroundColor Yellow
Write-Host ""

Write-Host "Pushing to rtsp://127.0.0.1:8554/main ..." -ForegroundColor White
$ffmpegArgs1 = @(
    "-re",
    "-i", $testVideo,
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-tune", "zerolatency",
    "-f", "rtsp",
    "rtsp://127.0.0.1:8554/main"
)
Start-Process -FilePath $ffmpegExe -ArgumentList $ffmpegArgs1 -PassThru | Out-Null

Start-Sleep -Seconds 2

Write-Host "Pushing to rtsp://127.0.0.1:8555/aux ..." -ForegroundColor White
$ffmpegArgs2 = @(
    "-re",
    "-i", $testVideo,
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-tune", "zerolatency",
    "-f", "rtsp",
    "rtsp://127.0.0.1:8555/aux"
)
Start-Process -FilePath $ffmpegExe -ArgumentList $ffmpegArgs2 -PassThru | Out-Null

Start-Sleep -Seconds 3

# 验证进程是否运行
Write-Host ""
Write-Host "Verifying running processes..." -ForegroundColor Yellow
$mediamtxProcesses = Get-Process -Name mediamtx -ErrorAction SilentlyContinue
if ($mediamtxProcesses) {
    Write-Host "MediaMTX instances running: $($mediamtxProcesses.Count)" -ForegroundColor Green
    foreach ($proc in $mediamtxProcesses) {
        Write-Host "  - PID $($proc.Id)" -ForegroundColor White
    }
} else {
    Write-Host "[ERROR] No MediaMTX processes running" -ForegroundColor Red
}

$ffmpegProcesses = Get-Process -Name ffmpeg -ErrorAction SilentlyContinue
if ($ffmpegProcesses) {
    Write-Host "FFmpeg instances running: $($ffmpegProcesses.Count)" -ForegroundColor Green
    foreach ($proc in $ffmpegProcesses) {
        Write-Host "  - PID $($proc.Id)" -ForegroundColor White
    }
} else {
    Write-Host "[ERROR] No FFmpeg processes running" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Test Started Successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "RTSP Stream URLs:" -ForegroundColor Yellow
Write-Host "  Main: rtsp://127.0.0.1:8554/main" -ForegroundColor White
Write-Host "  Aux: rtsp://127.0.0.1:8555/aux" -ForegroundColor White
Write-Host ""
Write-Host "Verification Methods:" -ForegroundColor Yellow
Write-Host "  ffplay rtsp://127.0.0.1:8554/main" -ForegroundColor White
Write-Host "  ffplay rtsp://127.0.0.1:8555/aux" -ForegroundColor White
Write-Host ""
Write-Host "Or open in browser:" -ForegroundColor Yellow
Write-Host "  http://127.0.0.1:8888/main" -ForegroundColor White
Write-Host "  http://127.0.0.1:8889/aux" -ForegroundColor White
Write-Host ""
Write-Host "Press Enter to stop all processes..." -ForegroundColor Cyan
Read-Host | Out-Null

Write-Host ""
Write-Host "Stopping test processes..." -ForegroundColor Yellow
try {
    Stop-Process -Name "ffmpeg" -Force -ErrorAction SilentlyContinue
    Stop-Process -Name "mediamtx" -Force -ErrorAction SilentlyContinue
    Write-Host "All processes stopped" -ForegroundColor Green
} catch {
    Write-Host "Error stopping processes: $_" -ForegroundColor Red
}