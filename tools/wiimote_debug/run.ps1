param(
    [switch]$Mock
)

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

& .\.venv\Scripts\Activate.ps1
pip install -q -r backend/requirements.txt

$backendJob = Start-Job {
    Set-Location $using:Root\backend
    & $using:Root\.venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000
}

Push-Location frontend
if (-not (Test-Path "node_modules")) {
    npm install
}
npm run dev
Pop-Location

Stop-Job $backendJob -ErrorAction SilentlyContinue
Remove-Job $backendJob -ErrorAction SilentlyContinue
