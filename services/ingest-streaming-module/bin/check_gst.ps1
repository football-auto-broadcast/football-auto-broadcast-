Set-Location "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
Write-Host "========== Check GStreamer plugins =========="
$pluginDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin\lib\gstreamer-1.0"
Write-Host "Plugin directory:" $pluginDir
if (Test-Path $pluginDir) {
    $appPlugin = Get-ChildItem $pluginDir -Filter "*app*"
    $parsePlugin = Get-ChildItem $pluginDir -Filter "*parse*"
    $encPlugin = Get-ChildItem $pluginDir -Filter "*x264*"
    $convertPlugin = Get-ChildItem $pluginDir -Filter "*videoconvert*"
    $scalePlugin = Get-ChildItem $pluginDir -Filter "*videoscale*"
    Write-Host "appsrc plugin:" $appPlugin.Name
    Write-Host "parse plugin:" $parsePlugin.Name
    Write-Host "x264 plugin:" $encPlugin.Name
    Write-Host "videoconvert plugin:" $convertPlugin.Name
    Write-Host "videoscale plugin:" $scalePlugin.Name
} else {
    Write-Host "ERROR: plugin directory not found!"
}
Write-Host ""
Write-Host "========== Check environment variables =========="
Write-Host "GST_PLUGIN_PATH:" $env:GST_PLUGIN_PATH
Write-Host "GSTREAMER_1_0_ROOT_X86_64:" $env:GSTREAMER_1_0_ROOT_X86_64
Write-Host ""
