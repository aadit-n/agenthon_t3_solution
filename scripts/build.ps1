param([string]$Image = 'agenthon-t3:latest')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
docker build --platform linux/amd64 -t $Image $projectRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker build --platform linux/amd64 --target baseline -t agenthon-t3-baseline:local $projectRoot
exit $LASTEXITCODE
