param(
    [string]$Prefix = "$env:LOCALAPPDATA\Lia",
    [switch]$AddToPath,
    [switch]$NoBuild,
    [switch]$Uninstall,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Help {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File scripts\install.ps1 [-Prefix <path>] [-AddToPath] [-NoBuild] [-Uninstall]

Options:
  -Prefix <path>  Install prefix, default: %LOCALAPPDATA%\Lia
  -AddToPath      Add the install bin directory to the user PATH
  -NoBuild        Skip build and install the existing build\lia.exe binary
  -Uninstall      Remove lia.exe from the selected prefix
  -Help           Show this help
"@
}

if ($Help) {
    Show-Help
    exit 0
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Resolve-Path (Join-Path $scriptDir "..")
$binDir = Join-Path $Prefix "bin"
$target = Join-Path $binDir "lia.exe"

if ($Uninstall) {
    if (Test-Path $target) {
        Remove-Item $target -Force
        Write-Host "Removed $target"
    } else {
        Write-Host "Lia is not installed at $target"
    }
    exit 0
}

$make = @("make", "mingw32-make", "gmake") |
    Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
    Select-Object -First 1

if (-not $make) {
    throw "make, mingw32-make, or gmake is required. Install MinGW-w64/MSYS2 or another C build toolchain first."
}

Push-Location $projectDir
try {
    if (-not $NoBuild) {
        & $make build
    }
} finally {
    Pop-Location
}

$exe = Join-Path $projectDir "build\lia.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $projectDir "build\lia"
}
if (-not (Test-Path $exe)) {
    throw "Built binary was not found under build\. Run make build and try again."
}

New-Item -ItemType Directory -Force $binDir | Out-Null
Copy-Item $exe $target -Force
Write-Host "Installed Lia to $target"

if ($AddToPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $paths = @()
    if ($userPath) {
        $paths = $userPath -split ";"
    }
    if ($paths -notcontains $binDir) {
        $newPath = if ($userPath) { "$userPath;$binDir" } else { $binDir }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Host "Added $binDir to the user PATH. Open a new terminal to use lia."
    }
}
