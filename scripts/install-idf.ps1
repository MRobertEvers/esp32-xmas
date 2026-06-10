#Requires -Version 5.1
<#
.SYNOPSIS
    Install ESP-IDF and toolchains on Windows for this project.

.DESCRIPTION
    Clones the ESP-IDF release branch, installs Python/tools via install.ps1,
    and writes scripts/export-idf.ps1 so you can load the environment in any shell.

    Default install: ESP-IDF v5.4.2 with esp32s3 toolchain (matches this project).

.PARAMETER IdfVersion
    ESP-IDF git tag or branch (for example v5.4.2).

.PARAMETER IdfPath
    Where to clone ESP-IDF (default: %USERPROFILE%\esp\esp-idf).

.PARAMETER Target
    Chip target passed to install.ps1 (default: esp32s3).

.PARAMETER UseEim
    Use Espressif Installation Manager (EIM) via winget instead of git clone.
    Requires Windows 10/11 with winget available.

.EXAMPLE
    .\scripts\install-idf.ps1

.EXAMPLE
    .\scripts\install-idf.ps1 -IdfVersion v5.5.1

.EXAMPLE
    .\scripts\install-idf.ps1 -UseEim -IdfVersion v5.4.2
#>
[CmdletBinding()]
param(
    [string]$IdfVersion = "v5.4.2",
    [string]$IdfPath = (Join-Path $env:USERPROFILE "esp\esp-idf"),
    [string]$Target = "esp32s3",
    [switch]$UseEim
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Test-CommandExists {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WithEim {
    param([string]$Version)

    if (-not (Test-CommandExists "winget")) {
        throw "winget is not available. Install App Installer from the Microsoft Store, or run without -UseEim."
    }

    Write-Step "Installing Espressif Installation Manager (EIM CLI)"
    winget install --id Espressif.EIM-CLI `
        --accept-package-agreements `
        --accept-source-agreements

    if (-not (Test-CommandExists "eim")) {
        throw "EIM was installed but 'eim' is not on PATH. Open a new terminal and run: eim install -i $Version"
    }

    Write-Step "Installing ESP-IDF $Version with EIM"
    eim install -i $Version

    Write-Host ""
    Write-Host "EIM install finished." -ForegroundColor Green
    Write-Host "Open a new 'ESP-IDF PowerShell' shortcut from the Start menu, or run 'eim activate' if available."
}

function Install-WithGitClone {
    param(
        [string]$Version,
        [string]$Path,
        [string]$ChipTarget
    )

    if (-not (Test-CommandExists "git")) {
        throw @"
Git is not installed or not on PATH.
Install Git for Windows: https://git-scm.com/download/win
Then re-run this script.
"@
    }

    $parentDir = Split-Path -Parent $Path
    if (-not (Test-Path $parentDir)) {
        Write-Step "Creating $parentDir"
        New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
    }

    if (-not (Test-Path (Join-Path $Path ".git"))) {
        Write-Step "Cloning ESP-IDF $Version into $Path"
        git clone --recursive --branch $Version `
            https://github.com/espressif/esp-idf.git $Path
    }
    else {
        Write-Step "ESP-IDF repo already exists at $Path - fetching $Version"
        Push-Location $Path
        try {
            git fetch --tags --prune
            git checkout $Version
            git submodule update --init --recursive
        }
        finally {
            Pop-Location
        }
    }

    $installScript = Join-Path $Path "install.ps1"
    if (-not (Test-Path $installScript)) {
        throw "install.ps1 not found at $installScript"
    }

    Write-Step "Installing ESP-IDF tools for target: $ChipTarget"
    Write-Host "This downloads compilers and Python packages into %USERPROFILE%\.espressif"
    Write-Host "It may take several minutes."

    Push-Location $Path
    try {
        & $installScript $ChipTarget
        if ($LASTEXITCODE -ne 0) {
            throw "install.ps1 failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    $exportHelper = Join-Path $PSScriptRoot "export-idf.ps1"
    $exportLines = @(
        '# Load ESP-IDF into the current PowerShell session.'
        '# Usage: . .\scripts\export-idf.ps1'
        ''
        ('$IdfPath = "{0}"' -f $Path)
        ''
        'if (-not (Test-Path "$IdfPath\export.ps1")) {'
        '    throw "ESP-IDF export.ps1 not found at $IdfPath. Run .\scripts\install-idf.ps1 first."'
        '}'
        ''
        '. "$IdfPath\export.ps1"'
        ''
        'Write-Host "ESP-IDF environment loaded from $IdfPath" -ForegroundColor Green'
        'if (Get-Command idf.py -ErrorAction SilentlyContinue) {'
        '    Write-Host ("idf.py version: " + (idf.py --version)) -ForegroundColor DarkGray'
        '}'
    )
    Set-Content -Path $exportHelper -Value $exportLines -Encoding UTF8

    Write-Host ""
    Write-Host "ESP-IDF installed successfully." -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:"
    Write-Host "  1. Open a new PowerShell window in this project"
    Write-Host "  2. Load the environment:"
    Write-Host "       . .\scripts\export-idf.ps1"
    Write-Host "  3. Build and flash:"
    Write-Host "       idf.py set-target esp32s3"
    Write-Host "       idf.py build"
    Write-Host "       idf.py -p COMx flash monitor"
}

Write-Host "ESP-IDF installer for esp32-xmas"
Write-Host "Target chip : $Target"
Write-Host "IDF version : $IdfVersion"
Write-Host "IDF path    : $IdfPath"

if ($UseEim) {
    Install-WithEim -Version $IdfVersion
}
else {
    Install-WithGitClone -Version $IdfVersion -Path $IdfPath -ChipTarget $Target
}
