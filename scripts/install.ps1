param(
    [string]$Prefix = $(if ($env:LIA_PREFIX) { $env:LIA_PREFIX } elseif ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Lia" } else { Join-Path $HOME ".lia" }),
    [string]$Repo = $(if ($env:LIA_REPO) { $env:LIA_REPO } else { "mixserrm999/Lia" }),
    [string]$Version = $(if ($env:LIA_VERSION) { $env:LIA_VERSION } else { "latest" }),
    [string]$BootstrapVersion = $(if ($env:LIA_BOOTSTRAP_VERSION) { $env:LIA_BOOTSTRAP_VERSION } else { "0.1.2" }),
    [string]$Archive = $(if ($env:LIA_ARCHIVE) { $env:LIA_ARCHIVE } else { "" }),
    [switch]$FromSource,
    [switch]$NoBuild,
    [switch]$AddToPath,
    [switch]$NoPath,
    [switch]$Uninstall,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Show-Help {
    @"
Usage:
  powershell -ExecutionPolicy Bypass -File scripts\install.ps1 [options]
  irm https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.ps1 | iex

Options:
  -Prefix <path>     Install prefix, default: %LOCALAPPDATA%\Lia
  -Repo <owner/repo> GitHub repository, default: mixserrm999/Lia
  -Version <tag>     Release tag or version, default: latest
  -Archive <path-or-url>
                    Install from a local or remote release archive
  -FromSource        Build and install from the current source tree
  -NoBuild           With -FromSource, install the existing build\lia.exe binary
  -AddToPath         Accepted for compatibility; PATH is updated by default
  -NoPath            Do not update the user PATH
  -Uninstall         Remove lia.exe from the selected prefix
  -Help              Show this help

Environment:
  LIA_PREFIX         Default install prefix
  LIA_REPO           GitHub repository
  LIA_VERSION        Release tag or version
  LIA_BOOTSTRAP_VERSION
                    Fallback version used when GitHub Releases are unavailable
  LIA_ARCHIVE        Local or remote release archive
"@
}

function Fail($Message) {
    throw "lia installer: $Message"
}

function Test-Url($Value) {
    return $Value -match '^https?://'
}

function Normalize-Tag($Value) {
    if ($Value.StartsWith("v")) {
        return $Value
    }
    return "v$Value"
}

function Get-VersionFromTag($Tag) {
    if ($Tag.StartsWith("v")) {
        return $Tag.Substring(1)
    }
    return $Tag
}

function Get-LiaPlatform {
    if ($env:OS -ne "Windows_NT") {
        Fail "this installer currently supports Windows"
    }

    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    if ($arch -eq "X64") {
        return "windows-x64"
    }

    Fail "unsupported CPU '$arch'; this installer currently supports x64"
}

function Download-File($Uri, $OutFile) {
    Invoke-WebRequest -Uri $Uri -OutFile $OutFile
}

function Get-LatestTag {
    $metadata = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest"
    if (-not $metadata.tag_name) {
        Fail "GitHub latest release response did not include tag_name"
    }
    return [string]$metadata.tag_name
}

function Try-GetLatestTag {
    try {
        return Get-LatestTag
    } catch {
        return $null
    }
}

function Get-AssetUrl($Source, $Tag, $Name) {
    if ($Source -eq "release") {
        return "https://github.com/$Repo/releases/download/$Tag/$Name"
    }

    return "https://raw.githubusercontent.com/$Repo/$Tag/dist/$Name"
}

function Verify-Checksum($ArchivePath, $ArchiveName, $ChecksumPath) {
    if (-not (Test-Path $ChecksumPath)) {
        Write-Host "Could not download SHA256SUMS; skipping checksum verification"
        return
    }

    $escapedName = [regex]::Escape($ArchiveName)
    $line = Get-Content $ChecksumPath | Where-Object { $_ -match "\s+$escapedName$" } | Select-Object -First 1
    if (-not $line) {
        Fail "SHA256SUMS does not contain $ArchiveName"
    }

    $expected = ($line -split '\s+')[0].ToLowerInvariant()
    $actual = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        Fail "checksum mismatch for $ArchiveName"
    }

    Write-Host "$ArchiveName: OK"
}

function Add-ToUserPath($BinDir) {
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $paths = @()
    if ($currentPath) {
        $paths = $currentPath -split ";"
    }

    if ($paths -contains $BinDir) {
        return
    }

    $newPath = if ($currentPath) { "$currentPath;$BinDir" } else { $BinDir }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    $env:Path = "$BinDir;$env:Path"
    Write-Host "Added $BinDir to the user PATH. Open a new terminal to use lia."
}

function Install-Binary($SourceExe) {
    $binDir = Join-Path $Prefix "bin"
    $target = Join-Path $binDir "lia.exe"
    New-Item -ItemType Directory -Force $binDir | Out-Null
    Copy-Item $SourceExe $target -Force
    & $target --version | Out-Null
    Write-Host "Installed Lia to $target"

    if (-not $NoPath) {
        Add-ToUserPath $binDir
    } elseif (($env:Path -split ";") -notcontains $binDir) {
        Write-Host "Add this directory to PATH if needed: $binDir"
    }
}

function Install-FromSource($ProjectDir) {
    $make = @("make", "mingw32-make", "gmake") |
        Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
        Select-Object -First 1

    if (-not $make) {
        Fail "make, mingw32-make, or gmake is required for -FromSource"
    }

    Push-Location $ProjectDir
    try {
        if (-not $NoBuild) {
            & $make build
        }
    } finally {
        Pop-Location
    }

    $exe = Join-Path $ProjectDir "build\lia.exe"
    if (-not (Test-Path $exe)) {
        $exe = Join-Path $ProjectDir "build\lia"
    }
    if (-not (Test-Path $exe)) {
        Fail "built binary was not found under build\"
    }

    Install-Binary $exe
}

function Install-FromArchive {
    $platform = Get-LiaPlatform
    $tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force $tmpDir | Out-Null

    try {
        if ($Archive) {
            $archiveName = Split-Path $Archive -Leaf
            $archivePath = Join-Path $tmpDir $archiveName
            if (Test-Url $Archive) {
                Download-File $Archive $archivePath
            } else {
                if (-not (Test-Path $Archive)) {
                    Fail "archive not found: $Archive"
                }
                Copy-Item $Archive $archivePath
            }
        } else {
            $assetSource = "release"
            if ($Version -eq "latest") {
                $tag = Try-GetLatestTag
                if (-not $tag) {
                    $tag = Normalize-Tag $BootstrapVersion
                    $assetSource = "raw"
                }
            } else {
                $tag = Normalize-Tag $Version
            }

            $assetVersion = Get-VersionFromTag $tag
            $archiveName = "lia-$assetVersion-$platform.zip"
            $archivePath = Join-Path $tmpDir $archiveName
            try {
                Download-File (Get-AssetUrl $assetSource $tag $archiveName) $archivePath
            } catch {
                $assetSource = "raw"
                Download-File (Get-AssetUrl $assetSource $tag $archiveName) $archivePath
            }

            $checksumPath = Join-Path $tmpDir "SHA256SUMS"
            try {
                Download-File (Get-AssetUrl $assetSource $tag "SHA256SUMS") $checksumPath
                Verify-Checksum $archivePath $archiveName $checksumPath
            } catch {
                Remove-Item $checksumPath -Force -ErrorAction SilentlyContinue
                throw
            }
        }

        $extractDir = Join-Path $tmpDir "extract"
        New-Item -ItemType Directory -Force $extractDir | Out-Null

        if ($archiveName.EndsWith(".zip")) {
            Expand-Archive -Path $archivePath -DestinationPath $extractDir -Force
        } elseif ($archiveName.EndsWith(".tar.gz") -or $archiveName.EndsWith(".tgz")) {
            $tar = Get-Command tar -ErrorAction SilentlyContinue
            if (-not $tar) {
                Fail "tar is required to extract $archiveName"
            }
            & $tar -xzf $archivePath -C $extractDir
        } else {
            Fail "unsupported archive type: $archiveName"
        }

        $sourceExe = Get-ChildItem -Path $extractDir -Recurse -File -Filter "lia.exe" |
            Where-Object { $_.FullName -match "\\bin\\lia\.exe$" } |
            Select-Object -First 1
        if (-not $sourceExe) {
            Fail "archive does not contain bin\lia.exe"
        }

        Install-Binary $sourceExe.FullName
    } finally {
        Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($Help) {
    Show-Help
    exit 0
}

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

$scriptPath = $PSCommandPath
$projectDir = $null
if ($scriptPath) {
    $scriptDir = Split-Path -Parent $scriptPath
    $candidate = Resolve-Path (Join-Path $scriptDir "..") -ErrorAction SilentlyContinue
    if ($candidate -and (Test-Path (Join-Path $candidate "Makefile"))) {
        $projectDir = $candidate.Path
    }
}

if ($FromSource -or ($projectDir -and -not $Archive)) {
    if (-not $projectDir) {
        Fail "-FromSource requires running this script from a Lia source checkout"
    }
    Install-FromSource $projectDir
} else {
    Install-FromArchive
}
