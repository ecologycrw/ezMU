<#
.SYNOPSIS
    Configure, build and test ezMU on Windows with a CMake preset.

.DESCRIPTION
    Enters the Visual Studio 2026 x64 developer environment (so Ninja + MSVC work from any
    shell), makes sure VCPKG_ROOT is set, then runs the requested steps for the preset.

.EXAMPLE
    ./scripts/dev.ps1                                   # windows-msvc-debug, all steps
    ./scripts/dev.ps1 -Preset windows-msvc-release
    ./scripts/dev.ps1 -Step build
    ./scripts/dev.ps1 -Step test -TestFilter foundation
#>
[CmdletBinding()]
param(
    [string] $Preset = 'windows-msvc-debug',
    [ValidateSet('configure', 'build', 'test', 'all')]
    [string] $Step = 'all',
    [string] $TestFilter = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Enter-EzmuDevShell {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found. Install Visual Studio 2026.' }
    # VsDevCmd calls vswhere by name; make it resolvable.
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio installation with the C++ x64 toolset found.' }

    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function Initialize-VcpkgRoot {
    if ($env:VCPKG_ROOT) { return }
    $userValue = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
    if (-not $userValue) { throw 'VCPKG_ROOT is not set. See README.md (Building).' }
    $env:VCPKG_ROOT = $userValue
}

function Invoke-Native {
    param([string] $Exe, [string[]] $Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
    }
}

Initialize-VcpkgRoot
Enter-EzmuDevShell

Push-Location $repoRoot
try {
    if ($Step -in 'configure', 'all') {
        Invoke-Native cmake @('--preset', $Preset)
    }
    if ($Step -in 'build', 'all') {
        Invoke-Native cmake @('--build', '--preset', $Preset)
    }
    if ($Step -in 'test', 'all') {
        $testArgs = @('--preset', $Preset)
        if ($TestFilter) { $testArgs += @('-R', $TestFilter) }
        Invoke-Native ctest $testArgs
    }
}
finally {
    Pop-Location
}
