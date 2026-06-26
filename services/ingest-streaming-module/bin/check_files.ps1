$binDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
$mainFile = "$binDir\stream_main.h264"
$auxFile = "$binDir\stream_aux.h264"

Write-Host "=== H.264 Temp File Status ==="
if (Test-Path $mainFile) {
    $size = (Get-Item $mainFile).Length
    Write-Host "stream_main.h264: $size bytes"
} else {
    Write-Host "stream_main.h264: NOT FOUND"
}
if (Test-Path $auxFile) {
    $size = (Get-Item $auxFile).Length
    Write-Host "stream_aux.h264: $size bytes"
} else {
    Write-Host "stream_aux.h264: NOT FOUND"
}
Write-Host ""

Write-Host "=== Suggested Fix ==="
Write-Host "Add 3-5 second delay between 'pipeline started' and 'ffmpeg start'"
Write-Host "Increase ffmpeg probesize/analyzeduration to ensure H.264 header detection"
Write-Host ""
