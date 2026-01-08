# VRClimbing Build Script
# Usage: .\build-vrclimbing.ps1 [-Config Release|Debug] [-Clean]

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Set VCPKG_ROOT if not already set
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg"
}

# Enter VS Developer Shell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"

# Navigate to project directory
$projectDir = $PSScriptRoot
Set-Location $projectDir

# Clean build directory if requested
if ($Clean -and (Test-Path "build")) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}

# Configure if build directory doesn't exist
if (-not (Test-Path "build")) {
    Write-Host "Configuring CMake..." -ForegroundColor Cyan
    cmake -B build -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-skse `
        -DVCPKG_OVERLAY_TRIPLETS="$projectDir/cmake"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
}

# Build
Write-Host "Building $Config..." -ForegroundColor Cyan
cmake --build build --config $Config

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild successful!" -ForegroundColor Green
    Write-Host "Output: $projectDir\build\$Config\VRClimbing.dll" -ForegroundColor Green
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
