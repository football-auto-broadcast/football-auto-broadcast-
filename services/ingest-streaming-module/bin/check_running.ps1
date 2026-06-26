$mtx = Get-Process -Name mediamtx -ErrorAction SilentlyContinue
$ingest = Get-Process -Name ingest-streaming-module -ErrorAction SilentlyContinue
Write-Host "MediaMTX processes:" $mtx.Count
foreach ($p in $mtx) { Write-Host "  PID:" $p.Id }
Write-Host "Ingest processes:" $ingest.Count
foreach ($p in $ingest) { Write-Host "  PID:" $p.Id }

$ports = @(8554, 8555, 8888, 8889)
foreach ($port in $ports) {
    $netstat = netstat -ano | Select-String ":$port "
    if ($netstat) { Write-Host "Port :$port active" }
    else { Write-Host "Port :$port free" }
}
Write-Host ""
