# PreToolUse (Edit|Write|MultiEdit): block edits to generated, third-party and protected files.
# Exit 2 blocks the tool call and returns the message to Claude.

$ErrorActionPreference = 'Stop'
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }

$path = $data.tool_input.file_path
if (-not $path) { exit 0 }

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.TrimEnd('\')
$full = [System.IO.Path]::GetFullPath($path)
if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { exit 0 }

$rel = $full.Substring($root.Length + 1) -replace '\\', '/'

$protectedDirs = @('build/', 'out/', 'vcpkg_installed/', 'third_party/', '.git/')
foreach ($dir in $protectedDirs) {
    if ($rel.StartsWith($dir, [StringComparison]::OrdinalIgnoreCase)) {
        [Console]::Error.WriteLine("[ezMU hook] '$rel' is inside '$dir', which must not be edited by hand (generated or third-party). Change the source that produces it instead.")
        exit 2
    }
}

if ($rel -ieq 'LICENSE') {
    [Console]::Error.WriteLine('[ezMU hook] LICENSE holds the official MPL-2.0 text and must not be edited (ADR 0004).')
    exit 2
}

exit 0
