# Emit the model and its animation as const C for the ESP32's flash.
#
# This is the same shape as extract-model.ps1, but its output is a translation
# unit rather than a partition image: tools/bake_model/bake_model.c says why the
# model goes to .rodata instead of being decoded at boot.
#
# It is wired into the build (see the top-level CMakeLists.txt) so that changing
# ANIM_MODEL or ANIM_SEQ re-bakes. It used to be run by hand, and the result was
# a build that silently kept rendering the previous model.

param(
    [string]$ClientcDir = "",
    [string]$AnimCacheDir = "",
    [string]$AnimRev = "osrs239",
    [string]$AnimModel = "",
    [string]$AnimSeq = "",
    [string]$OutFile = "",
    [int]$Ambient = 64,
    [int]$Attenuation = 768,
    [string]$LightDir = "-50,-10,-50",
    [string]$Gamma = "1.0"
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
if (-not $AnimCacheDir) { $AnimCacheDir = Join-Path $ClientcDir "cache.osrs239" }
if (-not $OutFile) { $OutFile = Join-Path $RepoRoot "build\gen\xmas_baked.c" }
if (-not $AnimModel) { throw "-AnimModel is required" }
if (-not (Test-Path $AnimCacheDir)) { throw "dat2 cache not found: $AnimCacheDir" }

$Third = Join-Path $ClientcDir "3rd"
$RsCacheDir = Join-Path $Third "rscache"

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

# The frame list, in play order, from the same all.seq extract-model.ps1 reads.
# A frame's id is not its index, so the order here is the order the device
# replays.
$frames = New-Object System.Collections.ArrayList
$delays = New-Object System.Collections.ArrayList
if ($AnimSeq) {
    $seqFile = Join-Path $ClientcDir "OSRS-Content\osrs239-content\configs\all.seq"
    if (-not (Test-Path $seqFile)) { throw "all.seq not found at $seqFile" }
    $inRecord = $false
    foreach ($line in [System.IO.File]::ReadLines($seqFile)) {
        $t = $line.TrimEnd()
        if ($t -eq "[$AnimSeq]") { $inRecord = $true; continue }
        if ($inRecord -and $t.StartsWith("[")) { break }
        if ($inRecord -and $t.StartsWith("frame=")) {
            # `frame=<packed id>,<hold ticks>`. BOTH fields matter: the id says
            # which pose, the second says how many 20 ms client cycles to hold
            # it for, and it lives ONLY here -- the frame archive does not carry
            # it. Dropping it leaves every frame at delay 0, which a player
            # renders as the whole sequence at 50 poses a second.
            $parts = $t.Substring(6) -split ","
            [void]$frames.Add($parts[0])
            if ($parts.Count -gt 1) { [void]$delays.Add($parts[1]) } else { [void]$delays.Add("0") }
        }
    }
    if ($frames.Count -eq 0) { throw "sequence [$AnimSeq] has no frames in $seqFile" }
}

$ToolDir = Join-Path $RepoRoot "tools\bake_model\build"
New-Item -ItemType Directory -Force -Path $ToolDir | Out-Null
$exe = Join-Path $ToolDir "bake_model.exe"

$src = Join-Path $RepoRoot "tools\bake_model\bake_model.c"
$rebuild = -not (Test-Path $exe)
if (-not $rebuild) {
    $rebuild = (Get-Item $src).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc
}
if ($rebuild) {
    Write-Host "Building bake_model.exe..." -ForegroundColor Cyan
    & $CcPath -std=c11 -O1 -w `
        "-I$RsCacheDir\include" "-I$RsCacheDir\src" "-I$RsCacheDir\tools\common" `
        "-I$RsCacheDir\src\datatypes" `
        "-I$Third\toridraw" "-I$Third\toridraw_rscache\include" `
        "-I$Third\bzip" "-I$Third\miniz" "-I$Third\xteas" "-I$Third" `
        $src `
        (Join-Path $RsCacheDir "tools\common\asset_access.c") `
        (Join-Path $RsCacheDir "tools\common\tool_profile.c") `
        (Join-Path $RsCacheDir "rscache_unity.c") `
        (Join-Path $Third "toridraw\toridraw_unity.c") `
        (Join-Path $Third "toridraw_rscache\toridraw_rscache_unity.c") `
        (Join-Path $Third "bzip\bzip.c") (Join-Path $Third "bzip\bzip_encode.c") `
        (Join-Path $Third "miniz\miniz.c") (Join-Path $Third "xteas\xteas.c") `
        -lm -o $exe
    if ($LASTEXITCODE -ne 0) { throw "failed to build bake_model.exe" }
}

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

Write-Host "Baking model $AnimModel / $AnimSeq ($($frames.Count) frames)" -ForegroundColor Cyan
# The lighting knobs are here rather than in the C because they are a LOOK,
# not a fact about the model: see report_contrast in bake_model.c for what
# each one does and how to tell when a value is only clipping.
$light = @("--ambient", $Ambient, "--attenuation", $Attenuation, "--light", $LightDir, "--gamma", $Gamma)
if ($delays.Count -gt 0) { $light += @("--delays", ($delays -join ",")) }
if ($frames.Count -gt 0) {
    & $exe $AnimCacheDir --rev $AnimRev --model $AnimModel --frames ($frames -join ",") @light $OutFile
} else {
    & $exe $AnimCacheDir --rev $AnimRev --model $AnimModel @light $OutFile
}
if ($LASTEXITCODE -ne 0) { throw "bake_model failed with exit code $LASTEXITCODE" }
