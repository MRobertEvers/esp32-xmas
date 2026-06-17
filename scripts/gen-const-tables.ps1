# Precompute toridraw lookup tables as const C source for ESP32 flash embedding.
#
# Usage: .\scripts\gen-const-tables.ps1 [-OutFile ...]

param(
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent
$RasterDir = Join-Path (Split-Path $RepoRoot -Parent) "3d-raster"
$ToolDir = Join-Path $RepoRoot "tools\gen_const_tables"
$ToolBuildDir = Join-Path $ToolDir "build"
$ToolExe = Join-Path $ToolBuildDir "gen_const_tables.exe"

if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot "build\gen\toridraw_tables_data.c"
}

if (-not (Test-Path (Join-Path $RasterDir "src\graphics\shared_tables.c"))) {
    throw "3d-raster shared_tables.c not found at $RasterDir"
}

if (-not (Test-Path $ToolExe)) {
    Write-Host "Building gen_const_tables host tool..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $ToolBuildDir | Out-Null
    Push-Location $ToolBuildDir
    try {
        cmake .. -DRASTER_DIR="$RasterDir"
        cmake --build . --config Release
        if (-not (Test-Path ".\Release\gen_const_tables.exe")) {
            throw "Build failed: gen_const_tables.exe not found"
        }
        Copy-Item ".\Release\gen_const_tables.exe" $ToolExe -Force
    }
    finally {
        Pop-Location
    }
}

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

Write-Host "Generating const lookup tables -> $OutFile" -ForegroundColor Cyan
& $ToolExe $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "gen_const_tables failed with exit code $LASTEXITCODE"
}

Write-Host "Const tables ready: $OutFile" -ForegroundColor Green
