$binDir = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin"
Set-Location $binDir

# Set environment variables for the process
[Environment]::SetEnvironmentVariable("GST_PLUGIN_PATH", "$binDir\lib\gstreamer-1.0", "Process")
[Environment]::SetEnvironmentVariable("PATH", "$binDir;" + [Environment]::GetEnvironmentVariable("PATH", "Process"), "Process")

Write-Host "=== Environment ==="
Write-Host "GST_PLUGIN_PATH:" $env:GST_PLUGIN_PATH
Write-Host "Current dir:" (Get-Location).Path
Write-Host ""
Write-Host "=== Starting ingest-streaming-module.exe ==="
Write-Host ""

& ".\ingest-streaming-module.exe"

Write-Host ""
Write-Host "Process exited."
