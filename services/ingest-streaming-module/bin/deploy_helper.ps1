Set-Location "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
$binDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
$gstDir = "D:\football-github-new\football-auto-broadcast-\third_party\gstreamer"
$mvsDir = "D:\football-github-new\football-auto-broadcast-\third_party\mvs_sdk\win64"

Write-Host "========== 部署依赖 DLL =========="
Write-Host "目标目录: $binDir"
Write-Host ""

Write-Host "[1/3] 复制 GStreamer 核心 DLL..."
if (Test-Path "$gstDir\bin") {
    $gstDlls = Get-ChildItem "$gstDir\bin" -Filter "*.dll"
    Write-Host "  源文件数量: $($gstDlls.Count)"
    $gstDlls | ForEach-Object { Copy-Item $_.FullName -Destination "$binDir\" -Force }
    $afterCount = (Get-ChildItem $binDir -Filter "*.dll").Count
    Write-Host "  [OK] 已复制 GStreamer DLL，当前 bin 目录 DLL 总数: $afterCount"
} else {
    Write-Host "  [WARN] 找不到 $gstDir\bin"
}
Write-Host ""

Write-Host "[2/3] 复制 GStreamer 插件 DLL..."
$pluginDir = "$binDir\lib\gstreamer-1.0"
if (Test-Path "$gstDir\lib\gstreamer-1.0") {
    if (-not (Test-Path $pluginDir)) {
        New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
    }
    $pluginDlls = Get-ChildItem "$gstDir\lib\gstreamer-1.0" -Filter "*.dll"
    Write-Host "  插件文件数量: $($pluginDlls.Count)"
    $pluginDlls | ForEach-Object { Copy-Item $_.FullName -Destination "$pluginDir\" -Force }
    $pluginCount = (Get-ChildItem $pluginDir -Filter "*.dll").Count
    Write-Host "  [OK] 已复制插件到 $pluginDir，数量: $pluginCount"
} else {
    Write-Host "  [WARN] 找不到 $gstDir\lib\gstreamer-1.0"
}
Write-Host ""

Write-Host "[3/3] 复制 MVS SDK DLL..."
if (Test-Path $mvsDir) {
    $mvsFiles = Get-ChildItem $mvsDir
    Write-Host "  MVS SDK 文件: $($mvsFiles.Count)"
    Copy-Item "$mvsDir\*" -Destination "$binDir\" -Force
    Write-Host "  [OK] 已复制 MVS SDK DLL"
} else {
    Write-Host "  [WARN] 找不到 $mvsDir"
}
Write-Host ""

Write-Host "========== 部署完成 =========="
$dllCount = (Get-ChildItem $binDir -Filter "*.dll").Count
Write-Host "bin 目录 DLL 总数: $dllCount"
if (Test-Path $pluginDir) {
    $pCount = (Get-ChildItem $pluginDir -Filter "*.dll").Count
    Write-Host "GStreamer 插件 DLL 总数: $pCount"
}
Write-Host ""
Write-Host "验证关键文件:"
$checks = @("ingest-streaming-module.exe", "mediamtx.exe", "ffmpeg.exe", "mediamtx_8554.yml", "mediamtx_8555.yml")
foreach ($c in $checks) {
    $exists = Test-Path "$binDir\$c"
    Write-Host "  $c - $(if ($exists) { '[OK]' } else { '[MISSING]' })"
}
Write-Host ""
