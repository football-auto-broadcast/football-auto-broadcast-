$src = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\x64\Release\ingest_streaming_service.exe"
$dst = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin\ingest_streaming_service.exe"
Copy-Item $src $dst -Force
Write-Host "Copied ingest_streaming_service.exe to bin"
$fi = Get-Item $dst
Write-Host "File size: $($fi.Length) bytes, Modified: $($fi.LastWriteTime)"
