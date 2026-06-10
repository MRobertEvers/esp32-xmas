# Load ESP-IDF into the current PowerShell session.
# Usage: . .\scripts\export-idf.ps1

$IdfPath = "C:\Users\mrobe\esp\esp-idf"

if (-not (Test-Path "$IdfPath\export.ps1")) {
    throw "ESP-IDF export.ps1 not found at $IdfPath. Run .\scripts\install-idf.ps1 first."
}

. "$IdfPath\export.ps1"

Write-Host "ESP-IDF environment loaded from $IdfPath" -ForegroundColor Green
if (Get-Command idf.py -ErrorAction SilentlyContinue) {
    Write-Host ("idf.py version: " + (idf.py --version)) -ForegroundColor DarkGray
}
