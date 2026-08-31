# Stage the blob the ESP32 flashes to its `model` partition.
#
# Three sources, in the order they win:
#
#   -AnimModel   a dat2 model plus a sequence's rig and frames -> an ANIMATED blob
#   -ModelFile   a raw .model archive from OSRS-Content        -> a static blob
#   -ModelId     a dat1 cache archive id                       -> a static blob
#
# Every one of them produces the same container (tools/extract_model/pack_anim.c
# writes it, main.c reads it); the static forms simply carry no framemap and no
# frames. The device checks the magic and version rather than assuming, so a
# partition left over from an older packer says so instead of being walked.
#
# The host tools decode what they pack, once, as a check: a blob the device
# cannot read is far cheaper to diagnose here than as a blank panel over a
# serial line.

param(
    [string]$ClientcDir = "",
    [int]$ModelId = 7,
    [string]$CacheDir = "",
    [string]$OutFile = "",
    [string]$ModelFile = "",
    [string]$AnimCacheDir = "",
    [string]$AnimRev = "osrs239",
    [string]$AnimModel = "",
    [string]$AnimSeq = ""
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
if (-not $CacheDir) {
    $CacheDir = Join-Path $ClientcDir "cache.rs289lc"
}
if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot "build\model.bin"
}

$RsCacheDir = Join-Path $ClientcDir "3rd\rscache"
if (-not (Test-Path (Join-Path $RsCacheDir "rscache_unity.c"))) {
    throw "rscache not found at $RsCacheDir (is -ClientcDir right?)"
}

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

$Third = Join-Path $ClientcDir "3rd"
$ToolDir = Join-Path $RepoRoot "tools\extract_model\build"
New-Item -ItemType Directory -Force -Path $ToolDir | Out-Null

function Build-Tool([string]$SourceName, [string]$ExeName) {
    $exe = Join-Path $ToolDir $ExeName
    if (Test-Path $exe) { return $exe }
    Write-Host "Building $ExeName against 3rd/rscache..." -ForegroundColor Cyan
    $extra = @()
    if ($SourceName -eq "pack_anim.c") {
        # pack_anim reaches for the tools' shared cache helpers; extract_model
        # does not, and linking them into it would drag the dat2 stack into a
        # tool that only opens a dat1 cache.
        $extra = @(
            (Join-Path $RsCacheDir "tools\common\asset_access.c"),
            (Join-Path $RsCacheDir "tools\common\tool_profile.c")
        )
    }
    & $CcPath -std=c11 -O1 -w `
        "-I$RsCacheDir\include" "-I$RsCacheDir\src" "-I$RsCacheDir\tools\common" `
        "-I$Third\bzip" "-I$Third\miniz" "-I$Third\xteas" "-I$Third" `
        (Join-Path $RepoRoot "tools\extract_model\$SourceName") `
        @extra `
        (Join-Path $RsCacheDir "rscache_unity.c") `
        (Join-Path $Third "bzip\bzip.c") (Join-Path $Third "bzip\bzip_encode.c") `
        (Join-Path $Third "miniz\miniz.c") (Join-Path $Third "xteas\xteas.c") `
        -o $exe
    if ($LASTEXITCODE -ne 0) { throw "failed to build $ExeName" }
    return $exe
}

$OutDir = Split-Path $OutFile -Parent
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

if ($AnimModel) {
    if (-not $AnimCacheDir) { $AnimCacheDir = Join-Path $ClientcDir "cache.osrs239" }
    if (-not (Test-Path $AnimCacheDir)) { throw "dat2 cache not found: $AnimCacheDir" }

    # The sequence's frame list comes out of OSRS-Content's all.seq, which is
    # plain text: `frame=<packed id>,<hold ticks>` in play order. Reading it
    # here beats decoding the seq config, because the pack is what a person
    # actually browses when choosing an animation by name.
    $seqFile = Join-Path $ClientcDir "OSRS-Content\osrs239-content\configs\all.seq"
    if (-not (Test-Path $seqFile)) { throw "all.seq not found at $seqFile" }

    $frames = New-Object System.Collections.ArrayList
$delays = New-Object System.Collections.ArrayList
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

    Write-Host "Packing $AnimSeq ($($frames.Count) frames) with model $AnimModel" -ForegroundColor Cyan
    $exe = Build-Tool "pack_anim.c" "pack_anim.exe"
    & $exe $AnimCacheDir --rev $AnimRev --model $AnimModel --frames ($frames -join ",") $OutFile
    if ($LASTEXITCODE -ne 0) { throw "pack_anim failed with exit code $LASTEXITCODE" }
    return
}

$exe = Build-Tool "extract_model.c" "extract_model.exe"
if ($ModelFile) {
    if (-not (Test-Path $ModelFile)) { throw "model file not found: $ModelFile" }
    & $exe --file $ModelFile $OutFile
} else {
    if (-not (Test-Path $CacheDir)) { throw "cache directory not found: $CacheDir" }
    & $exe $CacheDir $ModelId $OutFile
}
if ($LASTEXITCODE -ne 0) { throw "extract_model failed with exit code $LASTEXITCODE" }
