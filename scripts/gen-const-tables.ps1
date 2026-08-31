# Emit ToriDraw's lookup tables as const C, for the ESP32's flash.
#
# ToriDraw builds its palette and trigonometry at ToriDraw_Init, which is 185 KB
# of writable storage at 16bpp -- more than half this part's RAM, all of it
# immutable after the first millisecond. Compiled `const` the same bytes sit in
# .rodata, which the linker script puts in flash.
#
# The generator is 3rd/toridraw/tools/toridraw_tables_gen.c: one host source
# file that INCLUDES shared_tables.c and calls its own builders, so the emitted
# values are the values by construction rather than by a second implementation
# that drifts. It must be compiled with the same -DTORIDRAW_PIXEL_FORMAT the
# firmware uses -- the palette's element is a toripixel_t, and a unit generated
# for one format is the wrong width for another. The emitted file carries a
# _Static_assert on that, so a mismatch is a compile error rather than a screen
# of wrong colours.
#
# Usage: .\scripts\gen-const-tables.ps1 -ClientcDir ... -PixelFormat ... -OutFile ...

param(
    [string]$ClientcDir = "",
    [string]$PixelFormat = "TORIDRAW_PF_RGB565_BE",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent

if (-not $ClientcDir) {
    # The vendored submodule, not a sibling checkout. CMake passes
    # -ClientcDir explicitly; this is only the fallback for running the script
    # by hand, and it should agree with the build rather than with whatever
    # happens to sit next to the repo.
    $ClientcDir = Join-Path $RepoRoot "3rd\oldschool-clientc"
}
if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot "build\gen\toridraw_tables_precomputed.c"
}

$ToriDrawDir = Join-Path $ClientcDir "3rd\toridraw"
$GenSrc = Join-Path $ToriDrawDir "tools\toridraw_tables_gen.c"

if (-not (Test-Path $GenSrc)) {
    throw "toridraw_tables_gen.c not found at $GenSrc (is -ClientcDir right?)"
}

# A host compiler, not the cross one. Prefer whatever is on PATH; fall back to
# the mingw64 toolchain oldschool-clientc vendors, which is what this repo's
# other host tooling uses.
# A HOST compiler, not the cross one -- these tools run here, not on the part.
#
# The vendored submodule does NOT supply this: oldschool-clientc gitignores its
# `toolchains/` directory, so resolving gcc relative to $ClientcDir worked only
# while that pointed at a full working copy. Search the places it can actually
# be instead, and say which ones were tried when it is in none of them.
$CcCandidates = @()
$Cc = (Get-Command gcc -ErrorAction SilentlyContinue)
if ($Cc) { $CcCandidates += $Cc.Source }
if ($env:HOST_CC) { $CcCandidates += $env:HOST_CC }
$CcCandidates += (Join-Path $RepoRoot "..\oldschool-clientc\toolchains\mingw64\bin\gcc.exe")
$CcCandidates += (Join-Path $ClientcDir "toolchains\mingw64\bin\gcc.exe")

$CcPath = $CcCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $CcPath) {
    throw ("no host C compiler. Put gcc on PATH, set HOST_CC, or vendor one. Tried:`n  " +
           ($CcCandidates -join "`n  "))
}

$ToolDir = Join-Path $RepoRoot "tools\gen_const_tables\build"
$ToolExe = Join-Path $ToolDir "tables_gen.exe"
New-Item -ItemType Directory -Force -Path $ToolDir | Out-Null

Write-Host "Building the table generator for $PixelFormat..." -ForegroundColor Cyan
& $CcPath -std=c11 -O2 -w "-DTORIDRAW_PIXEL_FORMAT=$PixelFormat" "-I$ToriDrawDir" `
    $GenSrc -lm -o $ToolExe
if ($LASTEXITCODE -ne 0) { throw "failed to build the table generator" }

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

Write-Host "Generating const lookup tables -> $OutFile" -ForegroundColor Cyan
# The generator writes to stdout. -Encoding ascii, because the emitted file is
# plain C and a BOM in front of it is a compile error nobody enjoys.
& $ToolExe | Out-File -FilePath $OutFile -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw "the table generator failed" }

Write-Host "Const tables ready: $OutFile" -ForegroundColor Green
