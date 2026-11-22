# scripts/install.ps1
# ────────────────────
# Installs axon.exe and adds it to the system PATH on Windows.
# Run as Administrator for system-wide install,
# or without for current user only.
#
# Usage:
#   .\scripts\install.ps1              # installs from .\build\axon.exe
#   .\scripts\install.ps1 -Binary ".\build\axon.exe" -InstallDir "C:\axon"

param(
    [string]$Binary     = ".\build\axon.exe",
    [string]$InstallDir = "$env:ProgramFiles\Axon"
)

Write-Host "╔══════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Axon Language — Install             ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Cyan

# ── Verify binary exists ──────────────────────────────────────
if (-not (Test-Path $Binary)) {
    Write-Host "❌ Binary not found: $Binary" -ForegroundColor Red
    Write-Host "   Build first with: .\scripts\build.ps1 release"
    exit 1
}

# ── Create install directory ──────────────────────────────────
if (-not (Test-Path $InstallDir)) {
    Write-Host "Creating: $InstallDir"
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
}

# ── Copy binary ───────────────────────────────────────────────
$Dest = Join-Path $InstallDir "axon.exe"
Write-Host "Installing axon → $Dest"
Copy-Item -Force $Binary $Dest

# ── Add to PATH ───────────────────────────────────────────────
$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if ($IsAdmin) {
    # System-wide PATH (all users)
    $Scope = [System.EnvironmentVariableTarget]::Machine
    $PathType = "System"
} else {
    # Current user PATH
    $Scope = [System.EnvironmentVariableTarget]::User
    $PathType = "User"
}

$CurrentPath = [System.Environment]::GetEnvironmentVariable("PATH", $Scope)

if ($CurrentPath -notlike "*$InstallDir*") {
    Write-Host "Adding $InstallDir to $PathType PATH..."
    [System.Environment]::SetEnvironmentVariable(
        "PATH",
        "$CurrentPath;$InstallDir",
        $Scope
    )
    Write-Host "✅ PATH updated." -ForegroundColor Green
    Write-Host ""
    Write-Host "  Restart your terminal for PATH to take effect." -ForegroundColor Yellow
} else {
    Write-Host "✅ Already in PATH." -ForegroundColor Green
}

# ── Verify ────────────────────────────────────────────────────
Write-Host ""
Write-Host "Testing installation..."
$env:PATH = "$env:PATH;$InstallDir"

try {
    $out = & "$Dest" version 2>&1
    Write-Host $out
    Write-Host ""
    Write-Host "✅ axon installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Cyan
    Write-Host "  axon version"
    Write-Host "  axon run   myfile.axon"
    Write-Host "  axon check myfile.axon"
    Write-Host "  axon test"
} catch {
    Write-Host "⚠️  Installation complete but could not verify. Try: axon version" -ForegroundColor Yellow
}
