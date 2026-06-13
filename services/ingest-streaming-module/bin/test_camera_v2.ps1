$bin = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
Set-Location $bin

Write-Host "=== Step 1: Copy new .exe ==="
Copy-Item "$bin\..\x64\Release\ingest_streaming_service.exe" "$bin\ingest_streaming_service.exe" -Force
$fi = Get-Item "$bin\ingest_streaming_service.exe"
Write-Host "  ingest_streaming_service.exe: $($fi.Length) bytes, modified: $($fi.LastWriteTime)"

Write-Host ""
Write-Host "=== Step 2: Kill existing processes ==="
$names = @("ingest-streaming-module", "mediamtx", "ffmpeg", "ffplay")
foreach ($n in $names) {
    $ps = Get-Process -Name $n -ErrorAction SilentlyContinue
    if ($ps) { foreach ($p in $ps) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
}
Start-Sleep -Seconds 2
Write-Host "  Done"

Write-Host ""
Write-Host "=== Step 3: Set GStreamer environment ==="
$env:GST_PLUGIN_PATH = "$bin\lib\gstreamer-1.0"
Write-Host "  GST_PLUGIN_PATH = $env:GST_PLUGIN_PATH"

Write-Host ""
Write-Host "=== Step 4: Launch MediaMTX Instance 1 (port 8554, /main) ==="
$mtx1 = Start-Process -FilePath "$bin\mediamtx.exe" -ArgumentList "$bin\mediamtx_8554.yml" -NoNewWindow -PassThru -RedirectStandardError "$bin\mtx1_err.log" -RedirectStandardOutput "$bin\mtx1_out.log"
Write-Host "  Started PID=$($mtx1.Id)"
Start-Sleep -Seconds 2

Write-Host ""
Write-Host "=== Step 5: Launch MediaMTX Instance 2 (port 8555, /aux) ==="
$mtx2 = Start-Process -FilePath "$bin\mediamtx.exe" -ArgumentList "$bin\mediamtx_8555.yml" -NoNewWindow -PassThru -RedirectStandardError "$bin\mtx2_err.log" -RedirectStandardOutput "$bin\mtx2_out.log"
Write-Host "  Started PID=$($mtx2.Id)"
Start-Sleep -Seconds 2

Write-Host ""
Write-Host "=== Step 6: Verify MediaMTX ports ==="
try {
    $netstat = (netstat -ano 2>$null) | Select-String -Pattern "LISTENING"
    $ports = $netstat | Select-String -Pattern ":8554|:8555"
} catch { }
foreach ($p in $ports) { Write-Host "  $p" }

Write-Host ""
Write-Host "=== Step 7: Starting ingest-streaming-module (camera capture) ==="
Write-Host "  GStreamer uses rtspclientsink to push directly to MediaMTX (no FFmpeg)"
$ingest = Start-Process -FilePath "$bin\ingest_streaming_service.exe" -NoNewWindow -PassThru -RedirectStandardError "$bin\ingest_err.log" -RedirectStandardOutput "$bin\ingest_out.log"
Write-Host "  Started PID=$($ingest.Id)"

Write-Host ""
Write-Host "=== Waiting 12 seconds for pipeline warmup ==="
for ($i = 1; $i -le 12; $i++) {
    Start-Sleep -Seconds 1
    if ($i % 3 -eq 0) {
        Write-Host "  [$i/12s] ingest process running=$(if($ingest.HasExited){'NO-EXITED'}else{'YES'})"
    }
}

Write-Host ""
Write-Host "=== Ingest process status ==="
if ($ingest.HasExited) {
    Write-Host "  PROCESS EXITED with code: $($ingest.ExitCode)"
} else {
    Write-Host "  PID=$($ingest.Id) RUNNING, Memory=$([math]::Round($ingest.WorkingSet64/1MB,1))MB"
}

Write-Host ""
Write-Host "=== Pipeline verification ==="
Write-Host "  Check ingest_out.log for: 'pushed ... frames' messages"
Write-Host "  Look for 'rtspclientsink' errors if any"
Write-Host ""
Write-Host "=== Playback instructions (run in separate CMD windows) ==="
Write-Host "  MAIN camera: ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/main"
Write-Host "  AUX  camera: ffplay -rtsp_transport tcp rtsp://127.0.0.1:8555/aux"
Write-Host ""
Write-Host "=== Test complete. Ingest process left running for playback. ==="
