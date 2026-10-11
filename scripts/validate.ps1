param([string]$Image = 'agenthon-t3:latest', [string]$OutputDirectory = 'results\v4-final-public')
$ErrorActionPreference = 'Stop'
$env:OPENBLAS_NUM_THREADS='1'
$env:OMP_NUM_THREADS='1'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = Join-Path $projectRoot '.venv-validation\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    py -3.13 -m venv (Join-Path $projectRoot '.venv-validation')
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.13 is required for the organizer toolkit' }
    & $pythonPath -m pip install 'qfbench2-common[data] @ git+https://github.com/Agenthon-2026/Agenthon2026-public.git@v2.5.1#subdirectory=common'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $pythonPath (Join-Path $PSScriptRoot 'prepare_public.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $pythonPath -m pip wheel --no-deps --wheel-dir (Join-Path $projectRoot 'validation-wheels') 'qfbench2-common @ git+https://github.com/Agenthon-2026/Agenthon2026-public.git@v2.5.1#subdirectory=common'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
docker build -t agenthon-t3-validator:local (Join-Path $projectRoot 'validation-wheels')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $pythonPath (Join-Path $PSScriptRoot 'run_public.py') --image $Image --isolated-scoring --output-dir (Join-Path $projectRoot $OutputDirectory)
exit $LASTEXITCODE
