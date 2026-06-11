# Precompute the toridraw HSL16->RGB lookup table for ESP32 flash embedding.
#
# Usage: .\scripts\gen-hsl16-table.ps1 [-OutFile ...]

param(
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent
$RasterDir = Join-Path (Split-Path $RepoRoot -Parent) "3d-raster"
$ToolDir = Join-Path $RepoRoot "tools\gen_hsl16_table"
$ToolBuildDir = Join-Path $ToolDir "build"
$ToolExe = Join-Path $ToolBuildDir "gen_hsl16_table.exe"

if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot "build\hsl16_rgb_table.bin"
}

if (-not (Test-Path (Join-Path $RasterDir "src\graphics\shared_tables.c"))) {
    throw "3d-raster shared_tables.c not found at $RasterDir"
}

if (-not (Test-Path $ToolExe)) {
    Write-Host "Building gen_hsl16_table host tool..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $ToolBuildDir | Out-Null
    Push-Location $ToolBuildDir
    try {
        cmake .. -DRASTER_DIR="$RasterDir"
        cmake --build . --config Release
        if (-not (Test-Path ".\Release\gen_hsl16_table.exe")) {
            throw "Build failed: gen_hsl16_table.exe not found"
        }
        Copy-Item ".\Release\gen_hsl16_table.exe" $ToolExe -Force
    }
    finally {
        Pop-Location
    }
}

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

Write-Host "Generating HSL16 lookup table -> $OutFile" -ForegroundColor Cyan
& $ToolExe $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "gen_hsl16_table failed with exit code $LASTEXITCODE"
}

Write-Host "HSL16 table ready: $OutFile" -ForegroundColor Green
