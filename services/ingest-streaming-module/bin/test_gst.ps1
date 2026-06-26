Set-Location "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
$binDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"

Write-Host "=== GStreamer Environment Test ==="
Write-Host ""

$pluginDir = "$binDir\lib\gstreamer-1.0"
Write-Host "Plugin directory:" $pluginDir

$env:GST_PLUGIN_PATH = $pluginDir
$env:PATH = "$binDir;" + $env:PATH

Write-Host "GST_PLUGIN_PATH set to:" $env:GST_PLUGIN_PATH
Write-Host ""

# Check if key plugin DLLs exist
$appDll = Test-Path "$pluginDir\gstapp.dll"
$x264Dll = Test-Path "$pluginDir\gstx264.dll"
$vscDll = Test-Path "$pluginDir\gstvideoconvertscale.dll"
$h264parseDll = Test-Path "$pluginDir\gstvideoparsersbad.dll"
$filesinkDll = Test-Path "$pluginDir\gstcoreelements.dll"

Write-Host "gstapp.dll (appsrc):" $appDll
Write-Host "gstx264.dll (x264enc):" $x264Dll
Write-Host "gstvideoconvertscale.dll:" $vscDll
Write-Host "gstvideoparsersbad.dll (h264parse):" $h264parseDll
Write-Host "gstcoreelements.dll (filesink):" $filesinkDll
Write-Host ""

# Try gst-inspect-1.0
$gstInspect = "$binDir\gst-inspect-1.0.exe"
if (Test-Path $gstInspect) {
    Write-Host "=== gst-inspect-1.0 found: test appsrc element ==="
    $output = & $gstInspect appsrc 2>&1
    if ($output -match "Factory Details") {
        Write-Host "[OK] appsrc element registered successfully"
    } else {
        Write-Host "[WARN] appsrc inspection output:"
        $output | ForEach-Object { Write-Host " " $_ }
    }
} else {
    Write-Host "[WARN] gst-inspect-1.0.exe not in bin directory"
}

Write-Host ""
Write-Host "=== Test Complete ==="
