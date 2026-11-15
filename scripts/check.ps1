<#
.SYNOPSIS
    Full local check of ezMU: build, tests (including architecture rules) and formatting.

.EXAMPLE
    ./scripts/check.ps1                      # windows-msvc-debug: build + tests + format check
    ./scripts/check.ps1 -Fix                 # apply clang-format instead of only checking
    ./scripts/check.ps1 -Tidy                # also run clang-tidy on engine sources
    ./scripts/check.ps1 -SkipBuild           # formatting (and tidy) only
#>
[CmdletBinding()]
param(
    [string] $Preset = 'windows-msvc-debug',
    [switch] $Fix,
    [switch] $Tidy,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\ezmu-tools.ps1')
$repoRoot = Get-EzmuRepoRoot
$failures = @()

# --- Build + tests --------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "==> Build and test ($Preset)" -ForegroundColor Cyan
    try {
        & (Join-Path $PSScriptRoot 'dev.ps1') -Preset $Preset -Step all
    }
    catch {
        $failures += "build/test: $($_.Exception.Message)"
    }
}

# --- Formatting -----------------------------------------------------------------------------
Write-Host '==> Formatting' -ForegroundColor Cyan
$clangFormat = Find-EzmuClangFormat
if (-not $clangFormat) {
    $failures += 'clang-format not found (install the VS "C++ Clang tools" component or set CLANG_FORMAT).'
}
else {
    $files = Get-EzmuFormattableFiles -RepoRoot $repoRoot
    Push-Location $repoRoot
    try {
        if ($Fix) {
            Invoke-EzmuNative $clangFormat (@('-i', '--style=file') + $files)
        }
        else {
            Invoke-EzmuNative $clangFormat (@('--dry-run', '--Werror', '--style=file') + $files) -NoThrow
            if ($LASTEXITCODE -ne 0) {
                $failures += 'formatting: run ./scripts/check.ps1 -Fix -SkipBuild'
            }
        }
    }
    finally {
        Pop-Location
    }
}

# --- Static analysis (optional) ---------------------------------------------------------------
if ($Tidy) {
    Write-Host '==> clang-tidy' -ForegroundColor Cyan
    $clangTidy = Find-EzmuClangTidy
    $buildDir = Join-Path $repoRoot "build\$Preset"
    if (-not $clangTidy) {
        $failures += 'clang-tidy not found.'
    }
    elseif (-not (Test-Path (Join-Path $buildDir 'compile_commands.json'))) {
        $failures += "clang-tidy: no compile_commands.json in $buildDir (build first)."
    }
    else {
        $sources = Get-EzmuFormattableFiles -RepoRoot $repoRoot |
            Where-Object { $_ -like '*.cpp' -and $_ -notlike '*/tests/*' }
        Push-Location $repoRoot
        try {
            # MSVC-only flags in compile_commands.json are unknown to clang; ignore them.
            $tidyArgs = @('-p', $buildDir, '--quiet', '--warnings-as-errors=*',
                '--extra-arg=-Wno-unused-command-line-argument')
            Invoke-EzmuNative $clangTidy ($tidyArgs + $sources) -NoThrow
            if ($LASTEXITCODE -ne 0) { $failures += 'clang-tidy reported issues.' }
        }
        finally {
            Pop-Location
        }
    }
}

# --- Summary --------------------------------------------------------------------------------
if ($failures.Count -gt 0) {
    Write-Host "`nCHECK FAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host "`nAll checks passed." -ForegroundColor Green
