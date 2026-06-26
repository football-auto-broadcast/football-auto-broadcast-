$bin = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"

Write-Host "=== Pixel Type Diagnosis ==="
Write-Host "Camera F92514845: srcPixelType=17301505 (0x1080001) = BayerGB8, size=5038848"
Write-Host "  Expected RGB24 for 2592x1944: $(2592 * 1944 * 3) bytes = 15,116,544"
Write-Host ""
Write-Host "Camera D91363830: srcPixelType=34603058 (0x2100002) = BayerGB12, size=10077696"
Write-Host "  Expected RGB24 for 2592x1944: $(2592 * 1944 * 3) bytes"
Write-Host ""
Write-Host "=== Problem Summary ==="
Write-Host "1. MV_CC_ConvertPixelType() FAILED (ret=-2147483636) - SDK doesn't support this conversion"
Write-Host "2. Fallback: raw Bayer data (5MB/10MB) copied to RGB24 buffer"
Write-Host "3. PushFrame() checks: size(5038848) != width*height*3(15116544) -> FRAME DROPPED!"
Write-Host "4. Result: 0 bytes in H.264 file -> FFmpeg can't decode -> RTSP no stream"
Write-Host ""
Write-Host "=== GStreamer Plugin Check for Bayer support ==="
$pluginDir = "$bin\lib\gstreamer-1.0"
if (Test-Path $pluginDir) {
    $dlls = Get-ChildItem $pluginDir -Filter "*.dll"
    $bayer = $dlls | Where-Object { $_.Name -match "bayer|raw|video" }
    foreach ($d in $bayer) { Write-Host "  $($d.Name)" }
}
Write-Host ""
