Get-Process -Name "ffmpeg" -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process -Name "ffplay" -ErrorAction SilentlyContinue | Stop-Process -Force
$mtxCount = (Get-Process -Name "mediamtx" -ErrorAction SilentlyContinue).Count
Write-Host "Stopped local ffmpeg/ffplay. MediaMTX retained:" $mtxCount "instances"
