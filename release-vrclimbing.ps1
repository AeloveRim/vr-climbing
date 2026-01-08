# VRClimbing Release Script
# Usage: .\release-vrclimbing.ps1
# Builds Release config and packages DLL + PDB into a release zip

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$buildDir = "$projectDir\build"
$releaseDir = "$buildDir\Release"
$dllPath = "$releaseDir\VRClimbing.dll"
$pdbPath = "$releaseDir\VRClimbing.pdb"

# Get version from CMakeLists.txt or use default
$version = "1.0.0"
$zipName = "VR Climbing-$version.zip"
$zipPath = "$projectDir\$zipName"

# INI file from MO2 overwrite folder
$iniSourcePath = "C:\games\skyrim\VRDEV\overwrite\SKSE\Plugins\VRClimbing.ini"

Write-Host "VRClimbing Release Builder" -ForegroundColor Cyan
Write-Host "==========================" -ForegroundColor Cyan

# Step 1: Build Release
Write-Host "`nStep 1: Building Release configuration..." -ForegroundColor Yellow
& "$projectDir\build-vrclimbing.ps1" -Config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed! Aborting release." -ForegroundColor Red
    exit 1
}

# Step 2: Verify build outputs exist
Write-Host "`nStep 2: Verifying build outputs..." -ForegroundColor Yellow

if (-not (Test-Path $dllPath)) {
    Write-Host "ERROR: DLL not found at $dllPath" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $pdbPath)) {
    Write-Host "WARNING: PDB not found at $pdbPath (continuing without debug symbols)" -ForegroundColor Yellow
    $includePdb = $false
} else {
    $includePdb = $true
}

Write-Host "  Found: VRClimbing.dll" -ForegroundColor Green
if ($includePdb) {
    Write-Host "  Found: VRClimbing.pdb" -ForegroundColor Green
}

if (-not (Test-Path $iniSourcePath)) {
    Write-Host "WARNING: INI not found at $iniSourcePath (continuing without config)" -ForegroundColor Yellow
    $includeIni = $false
} else {
    $includeIni = $true
    Write-Host "  Found: VRClimbing.ini" -ForegroundColor Green
}

# Step 3: Create release zip
Write-Host "`nStep 3: Creating release package..." -ForegroundColor Yellow

# Remove existing zip if present
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

# Create temporary staging directory
$stagingDir = "$buildDir\release-staging"
$sksePluginsDir = "$stagingDir\SKSE\Plugins"

if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}

New-Item -ItemType Directory -Path $sksePluginsDir -Force | Out-Null

# Copy files to staging
Copy-Item $dllPath "$sksePluginsDir\VRClimbing.dll"
if ($includePdb) {
    Copy-Item $pdbPath "$sksePluginsDir\VRClimbing.pdb"
}
if ($includeIni) {
    Copy-Item $iniSourcePath "$sksePluginsDir\VRClimbing.ini"
}

# Create zip
Compress-Archive -Path "$stagingDir\*" -DestinationPath $zipPath -Force

# Cleanup staging
Remove-Item $stagingDir -Recurse -Force

# Step 4: Report success
Write-Host "`n==========================" -ForegroundColor Cyan
Write-Host "Release package created!" -ForegroundColor Green
Write-Host "  Output: $zipPath" -ForegroundColor Green

# Show zip contents
Write-Host "`nPackage contents:" -ForegroundColor Cyan
$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
foreach ($entry in $zip.Entries) {
    Write-Host "  $($entry.FullName)" -ForegroundColor White
}
$zip.Dispose()

Write-Host "`nDone!" -ForegroundColor Green
