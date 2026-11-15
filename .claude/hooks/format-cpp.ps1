# PostToolUse (Edit|Write|MultiEdit): apply clang-format to edited C++ files (ADR 0018).

$ErrorActionPreference = 'Stop'
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }

$path = $data.tool_input.file_path
if (-not $path -or -not (Test-Path $path)) { exit 0 }

$extension = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
if ($extension -notin '.hpp', '.cpp', '.h', '.inl') { exit 0 }

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.TrimEnd('\')
$full = [System.IO.Path]::GetFullPath($path)
if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { exit 0 }
$rel = $full.Substring($root.Length + 1) -replace '\\', '/'
if ($rel -like 'third_party/*' -or $rel -like 'build/*') { exit 0 }

. (Join-Path $root 'scripts\lib\ezmu-tools.ps1')
$clangFormat = Find-EzmuClangFormat
if (-not $clangFormat) {
    [Console]::Error.WriteLine('[ezMU hook] clang-format not found; file left unformatted.')
    exit 1
}

Push-Location $root
try {
    Invoke-EzmuNative $clangFormat @('-i', '--style=file', $full) -NoThrow | Out-Null
}
finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine("[ezMU hook] clang-format failed on '$rel'.")
    exit 1
}
exit 0
