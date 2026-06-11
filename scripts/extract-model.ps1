# Extract a dat1 model archive from the OSRS cache and write build/model.bin.
#
# Usage:
#   .\scripts\extract-model.ps1 [-ModelId 7] [-CacheDir ...] [-OutFile ...]

param(
    [int]$ModelId = 7,
    [string]$CacheDir = "",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent
$RasterDir = Join-Path (Split-Path $RepoRoot -Parent) "3d-raster"
$ToolDir = Join-Path $RepoRoot "tools\extract_model"
$ToolBuildDir = Join-Path $ToolDir "build"
$ToolExe = Join-Path $ToolBuildDir "extract_model.exe"

if (-not $CacheDir) {
    $CacheDir = Join-Path $RasterDir "cache"
}
if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot "build\model.bin"
}

if (-not (Test-Path $CacheDir)) {
    throw "Cache directory not found: $CacheDir"
}
if (-not (Test-Path (Join-Path $RasterDir "src\osrs\rscache"))) {
    throw "3d-raster rscache sources not found at $RasterDir"
}

if (-not (Test-Path $ToolExe)) {
    Write-Host "Building extract_model host tool..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $ToolBuildDir | Out-Null
    Push-Location $ToolBuildDir
    try {
        cmake .. -DRASTER_DIR="$RasterDir"
        cmake --build . --config Release
        if (-not (Test-Path ".\Release\extract_model.exe")) {
            throw "Build failed: extract_model.exe not found"
        }
        Copy-Item ".\Release\extract_model.exe" $ToolExe -Force
    }
    finally {
        Pop-Location
    }
}

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

Write-Host "Extracting model archive $ModelId from $CacheDir" -ForegroundColor Cyan
& $ToolExe $CacheDir $ModelId $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "extract_model failed with exit code $LASTEXITCODE"
}

Write-Host "Model binary ready: $OutFile" -ForegroundColor Green
