Set-Location "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
Write-Host "========== 检查当前运行进程 =========="
$mtx = Get-Process -Name "mediamtx" -ErrorAction SilentlyContinue
$ff = Get-Process -Name "ffmpeg" -ErrorAction SilentlyContinue
$ffp = Get-Process -Name "ffplay" -ErrorAction SilentlyContinue
Write-Host "MediaMTX 进程数: $($mtx.Count)"
foreach ($p in $mtx) { Write-Host "  - PID:$($p.Id)" }
Write-Host "FFmpeg 进程数: $($ff.Count)"
foreach ($p in $ff) { Write-Host "  - PID:$($p.Id)  CPU:$([math]::Round($p.CPU,1))s 内存:$([math]::Round($p.WorkingSet64/1MB,1))MB" }
Write-Host "ffplay 进程数: $($ffp.Count)"
Write-Host ""
Write-Host "========== 端口检查 =========="
netstat -ano | Select-String ":8554|:8555|:8888|:8889" | Select-Object -First 4
Write-Host ""
