$src = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\x64\Release\ingest-streaming-module.exe"
$dst = "D:\football-github-new\football-auto-broadcast-\services\ingest-streaming-module\bin\ingest-streaming-module.exe"
Copy-Item $src $dst -Force
Write-Host "Copied ingest-streaming-module.exe to bin"
$fi = Get-Item $dst
Write-Host "File size: $($fi.Length) bytes, Modified: $($fi.LastWriteTime)"
