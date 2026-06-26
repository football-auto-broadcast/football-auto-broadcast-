Get-Process -Name "ffmpeg" -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process -Name "ffplay" -ErrorAction SilentlyContinue | Stop-Process -Force
$mtxCount = (Get-Process -Name "mediamtx" -ErrorAction SilentlyContinue).Count
Write-Host "已停止本地视频推流/播放进程。保留 MediaMTX: $mtxCount 个实例"
