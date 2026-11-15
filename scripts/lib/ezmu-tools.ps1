# Shared helpers for ezMU developer scripts and Claude Code hooks. Dot-source this file.

function Get-EzmuRepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

# Runs a native executable, streaming stdout+stderr as plain text. Windows PowerShell 5.1 turns
# redirected native stderr into terminating errors under $ErrorActionPreference = 'Stop'; this
# avoids that. Sets $global:LASTEXITCODE and throws on failure unless -NoThrow.
function Invoke-EzmuNative {
    param(
        [Parameter(Mandatory)] [string] $Exe,
        [string[]] $Arguments = @(),
        [switch] $NoThrow
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { $_ }
        }
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    $global:LASTEXITCODE = $code
    if ($code -ne 0 -and -not $NoThrow) {
        throw "'$(Split-Path -Leaf $Exe) $($Arguments -join ' ')' failed with exit code $code."
    }
}

function Get-EzmuVsPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    return & $vswhere -latest -products * -property installationPath
}

# Locates an LLVM tool: $env:<OverrideVar>, then PATH, then the LLVM bundled with Visual Studio.
function Find-EzmuLlvmTool {
    param([string] $Name, [string] $OverrideVar)

    $override = [Environment]::GetEnvironmentVariable($OverrideVar)
    if ($override -and (Test-Path $override)) { return $override }

    $cmd = Get-Command "$Name.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vs = Get-EzmuVsPath
    if ($vs) {
        $bundled = Join-Path $vs "VC\Tools\Llvm\x64\bin\$Name.exe"
        if (Test-Path $bundled) { return $bundled }
    }
    return $null
}

function Find-EzmuClangFormat { return Find-EzmuLlvmTool -Name 'clang-format' -OverrideVar 'CLANG_FORMAT' }
function Find-EzmuClangTidy { return Find-EzmuLlvmTool -Name 'clang-tidy' -OverrideVar 'CLANG_TIDY' }

# Tracked and untracked (not ignored) C++ sources that clang-format applies to.
function Get-EzmuFormattableFiles {
    param([string] $RepoRoot)
    Push-Location $RepoRoot
    try {
        $files = git ls-files --cached --others --exclude-standard -- '*.hpp' '*.cpp' '*.h' '*.inl'
        return @($files | Where-Object { $_ -and -not $_.StartsWith('third_party/') })
    }
    finally {
        Pop-Location
    }
}
