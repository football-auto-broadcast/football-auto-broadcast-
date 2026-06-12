$binDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
$mainFile = "$binDir\stream_main.h264"
$auxFile = "$binDir\stream_aux.h264"
if (Test-Path $mainFile) { $mainSize = (Get-Item $mainFile).Length; Write-Host "stream_main.h264: $mainSize bytes" } else { Write-Host "stream_main.h264: not found" }
if (Test-Path $auxFile) { $auxSize = (Get-Item $auxFile).Length; Write-Host "stream_aux.h264: $auxSize bytes" } else { Write-Host "stream_aux.h264: not found" }
Write-Host ""
Write-Host "========== Analysis =========="
Write-Host "Expected bytes per frame at 2592x1944 RGB: $ (2592 * 1944 * 3)"
Write-Host "Expected bytes per frame at 2592x1944 Bayer8: $(2592 * 1944 * 1)"
Write-Host "If camera sends Bayer but appsrc expects RGB -> x264enc gets garbage -> NO output"
