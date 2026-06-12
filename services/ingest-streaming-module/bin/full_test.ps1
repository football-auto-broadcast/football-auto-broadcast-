$bin = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
Set-Location $bin

Write-Host "=== Step 1: Kill existing processes ==="
$names = @("ingest-streaming-module", "mediamtx", "ffmpeg", "ffplay")
foreach ($n in $names) {
    $ps = Get-Process -Name $n -ErrorAction SilentlyContinue
    if ($ps) {
        foreach ($p in $ps) {
            Write-Host "  Killing $($p.Name) PID=$($p.Id)"
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
Start-Sleep -Seconds 2
Write-Host "  Done"

Write-Host ""
Write-Host "=== Step 2: Clear stale temp files ==="
Remove-Item "$bin\stream_main.h264" -ErrorAction SilentlyContinue
Remove-Item "$bin\stream_aux.h264" -ErrorAction SilentlyContinue
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
Start-Sleep -Seconds 3

Write-Host ""
Write-Host "=== Step 6: Verify MediaMTX ports ==="
$ports = @()
try {
    $netstat = (netstat -ano 2>$null) | Select-String -Pattern "LISTENING"
    $ports = $netstat | Select-String -Pattern ":8554|:8555"
} catch {}
foreach ($p in $ports) { Write-Host "  $p" }

Write-Host ""
Write-Host "=== Step 7: Starting ingest-streaming-module (camera capture) ==="
Write-Host "  This will connect to cameras and produce H.264 output"
$ingest = Start-Process -FilePath "$bin\ingest-streaming-module.exe" -NoNewWindow -PassThru -RedirectStandardError "$bin\ingest_err.log" -RedirectStandardOutput "$bin\ingest_out.log"
Write-Host "  Started PID=$($ingest.Id)"

Write-Host ""
Write-Host "=== Waiting 15 seconds for camera pipeline to warm up ==="
Write-Host "  Waiting for camera initialization and frame production..."
for ($i = 1; $i -le 15; $i++) {
    Start-Sleep -Seconds 1
    if ($i % 5 -eq 0) {
        $mainFile = Get-Item "$bin\stream_main.h264" -ErrorAction SilentlyContinue
        $auxFile = Get-Item "$bin\stream_aux.h264" -ErrorAction SilentlyContinue
        $mainSize = if ($mainFile) { $mainFile.Length } else { 0 }
        $auxSize = if ($auxFile) { $auxFile.Length } else { 0 }
        Write-Host "  [$i/15s] main.h264=$mainSize bytes, aux.h264=$auxSize bytes"
    }
}

Write-Host ""
Write-Host "=== Final file check ==="
$mainFile = Get-Item "$bin\stream_main.h264" -ErrorAction SilentlyContinue
$auxFile = Get-Item "$bin\stream_aux.h264" -ErrorAction SilentlyContinue
Write-Host "  stream_main.h264: $(if ($mainFile) { $mainFile.Length } else { 'NOT FOUND' }) bytes"
Write-Host "  stream_aux.h264: $(if ($auxFile) { $auxFile.Length } else { 'NOT FOUND' }) bytes"

Write-Host ""
Write-Host "=== Ingest process status ==="
$ingestProc = Get-Process -Id $ingest.Id -ErrorAction SilentlyContinue
if ($ingestProc) {
    Write-Host "  PID=$($ingestProc.Id) RUNNING, Memory=$([math]::Round($ingestProc.WorkingSet64/1MB,1))MB"
} else {
    Write-Host "  PROCESS EXITED"
}

Write-Host ""
Write-Host "=== Playback instructions ==="
Write-Host "  MAIN camera: ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/main"
Write-Host "  AUX  camera: ffplay -rtsp_transport tcp rtsp://127.0.0.1:8555/aux"
Write-Host ""
