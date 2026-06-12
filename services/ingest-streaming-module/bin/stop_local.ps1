Write-Host "========== 停止本地视频测试进程 =========="
$ff = Get-Process -Name "ffmpeg" -ErrorAction SilentlyContinue
$ffp = Get-Process -Name "ffplay" -ErrorAction SilentlyContinue
Write-Host "停止 FFmpeg: $($ff.Count) 个进程"
foreach ($p in $ff) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Write-Host "  - 已停止 PID:$($p.Id)" }
Write-Host "停止 ffplay: $($ffp.Count) 个进程"
foreach ($p in $ffp) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Write-Host "  - 已停止 PID:$($p.Id)" }
Write-Host ""
Write-Host "保留 MediaMTX 实例 (将用于相机推流)"
$mtx = Get-Process -Name "mediamtx" -ErrorAction SilentlyContinue
Write-Host "当前 MediaMTX: $($mtx.Count) 个"
foreach ($p in $mtx) { Write-Host "  - PID:$($p.Id)" }
Write-Host ""
