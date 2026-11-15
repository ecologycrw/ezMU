# Stop: when C++/CMake sources have uncommitted changes, build and run the tests before Claude
# finishes. On failure, exit 2 returns the tail of the output to Claude so it fixes the problem.
# A state that already passed is remembered (by content hash) and not rebuilt again.

$ErrorActionPreference = 'Continue'
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { $data = $null }
if ($data -and $data.stop_hook_active) { exit 0 }

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$preset = if ($env:EZMU_HOOK_PRESET) { $env:EZMU_HOOK_PRESET } else { 'windows-msvc-debug' }
$pathspecs = @('*.cpp', '*.hpp', '*.h', '*.inl', '*.in', '*CMakeLists.txt', '*.cmake',
    'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json')

Push-Location $root
try {
    $changed = @(git status --porcelain --untracked-files=all -- @pathspecs)
    if ($changed.Count -eq 0) { exit 0 }

    # Fingerprint of the current change set: tracked diff + contents of untracked files.
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append((git diff HEAD -- @pathspecs | Out-String))
    foreach ($file in @(git ls-files --others --exclude-standard -- @pathspecs)) {
        [void]$builder.Append("`n--$file--`n").Append([System.IO.File]::ReadAllText((Join-Path $root $file)))
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($builder.ToString()))
    $hash = -join ($bytes | ForEach-Object { $_.ToString('x2') })

    $stampDir = Join-Path $root 'build'
    $stamp = Join-Path $stampDir ".ezmu-verified-$preset"
    if ((Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $hash)) { exit 0 }

    $dev = Join-Path $root 'scripts\dev.ps1'
    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $dev -Preset $preset 2>&1 |
        ForEach-Object { "$_" }
    $code = $LASTEXITCODE

    if ($code -eq 0) {
        if (-not (Test-Path $stampDir)) { New-Item -ItemType Directory $stampDir | Out-Null }
        Set-Content -Path $stamp -Value $hash -Encoding ascii
        exit 0
    }

    $tail = ($output | Select-Object -Last 80) -join "`n"
    [Console]::Error.WriteLine("[ezMU hook] Build or tests failed for preset '$preset'. Fix before finishing:`n$tail")
    exit 2
}
finally {
    Pop-Location
}
