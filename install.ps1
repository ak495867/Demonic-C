<#
.SYNOPSIS
    Installs the Demonic C (DMC) programming language on Windows.
.DESCRIPTION
    Downloads and installs the latest or specified release of Demonic C to $HOME\.dmc,
    and adds the binary directory to the user's PATH environment variable.
.EXAMPLE
    irm https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.ps1 | iex
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File install.ps1 -Version v1.0.0
#>

[CmdletBinding()]
param(
    [string]$Version = "latest",
    [string]$InstallDir = "$env:USERPROFILE\.dmc",
    [string]$LocalZip = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Write-DmcBanner {
    Write-Host @"
   ___                         _       _____ 
  / _ \___ __ _ ___  _ _  (_)__   /  __ \
 / // / -_)  ' \/ _ \/ ' \/ / _ \ / / / /
/____/\__/_/_/_/\___/_/_/_/_/\___//_/ /_/ 
Demonic C Systems Programming Language Installer
"@ -ForegroundColor Red
}

Write-DmcBanner

$Repo = "ak495867/Demonic-C"
$AssetPrefix = "dmc-windows-x86_64"

# Validate architecture
$arch = if ([IntPtr]::Size -eq 8) { "x86_64" } else { "x86" }
if ($arch -ne "x86_64") {
    Write-Error "Demonic C officially supports 64-bit Windows (x86_64). Architecture '$arch' is not supported."
    exit 1
}

$TempZip = Join-Path ([System.IO.Path]::GetTempPath()) "dmc-$([Guid]::NewGuid().ToString('N')).zip"

try {
    if ($LocalZip -ne "") {
        if (-not (Test-Path $LocalZip)) {
            Write-Error "Local package '$LocalZip' does not exist."
            exit 1
        }
        Write-Host "Installing from local package: $LocalZip" -ForegroundColor Cyan
        Copy-Item -Path $LocalZip -Destination $TempZip -Force
    } else {
        # Determine download URL
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13

        $DownloadUrl = ""
        $ResolvedVersion = $Version

        if ($Version -eq "latest") {
            Write-Host "Fetching latest release metadata from GitHub..." -ForegroundColor Cyan
            $ApiUrl = "https://api.github.com/repos/$Repo/releases/latest"
            try {
                $Release = Invoke-RestMethod -Uri $ApiUrl -Headers @{ "User-Agent" = "DMC-Installer" }
                $ResolvedVersion = $Release.tag_name
                $Asset = $Release.assets | Where-Object { $_.name -like "$AssetPrefix*.zip" } | Select-Object -First 1
                if ($Asset) {
                    $DownloadUrl = $Asset.browser_download_url
                }
            } catch {
                Write-Warning "Could not query GitHub API (rate limit or network): $_"
                Write-Host "Falling back to release redirect..." -ForegroundColor Yellow
                $DownloadUrl = "https://github.com/$Repo/releases/latest/download/$AssetPrefix.zip"
            }
        } else {
            $DownloadUrl = "https://github.com/$Repo/releases/download/$Version/$AssetPrefix.zip"
        }

        if (-not $DownloadUrl) {
            $DownloadUrl = "https://github.com/$Repo/releases/latest/download/$AssetPrefix.zip"
        }

        Write-Host "Downloading Demonic C ($ResolvedVersion)..." -ForegroundColor Cyan
        Write-Host "URL: $DownloadUrl" -ForegroundColor DarkGray
        Invoke-WebRequest -Uri $DownloadUrl -OutFile $TempZip -UseBasicParsing
    }

    # Prepare installation directory
    if (Test-Path $InstallDir) {
        Write-Host "Updating existing installation in $InstallDir..." -ForegroundColor Yellow
    } else {
        Write-Host "Installing to $InstallDir..." -ForegroundColor Cyan
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
    }

    # Safe in-place replacement function for Windows (handles running executables)
    function Safe-CopyFile($src, $dst) {
        $parent = Split-Path -Parent $dst
        if (-not (Test-Path $parent)) {
            New-Item -Path $parent -ItemType Directory -Force | Out-Null
        }
        try {
            Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop
        } catch {
            # File is locked (e.g. running process). Rename destination to .old and copy
            $old = "$dst.old"
            Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue
            Move-Item -LiteralPath $dst -Destination $old -Force -ErrorAction SilentlyContinue
            Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop
        }
    }

    # Extract to temporary staging folder first to avoid lock conflicts
    Write-Host "Extracting files..." -ForegroundColor Cyan
    $ExtractDir = Join-Path ([System.IO.Path]::GetTempPath()) "dmc-extract-$([Guid]::NewGuid().ToString('N'))"
    New-Item -Path $ExtractDir -ItemType Directory -Force | Out-Null
    Expand-Archive -Path $TempZip -DestinationPath $ExtractDir -Force

    # Flatten if archive contained a top-level dmc folder
    $SourceDir = $ExtractDir
    $NestedDmc = Join-Path $ExtractDir "dmc"
    if (Test-Path (Join-Path $NestedDmc "bin")) {
        $SourceDir = $NestedDmc
    }

    # Safely copy items into $InstallDir
    Get-ChildItem -Path $SourceDir -Recurse | ForEach-Object {
        $relPath = $_.FullName.Substring($SourceDir.Length).TrimStart('\', '/')
        $targetPath = Join-Path $InstallDir $relPath
        if ($_.PSIsContainer) {
            if (-not (Test-Path $targetPath)) {
                New-Item -Path $targetPath -ItemType Directory -Force | Out-Null
            }
        } else {
            Safe-CopyFile $_.FullName $targetPath
        }
    }

    # Cleanup temp extract folder
    Remove-Item -Path $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue

    # Ensure bin directory exists
    $BinDir = Join-Path $InstallDir "bin"
    if (-not (Test-Path $BinDir)) {
        New-Item -Path $BinDir -ItemType Directory -Force | Out-Null
    }

    # Check for dmc.exe in InstallDir vs InstallDir\bin
    $RootExe = Join-Path $InstallDir "dmc.exe"
    $BinExe = Join-Path $BinDir "dmc.exe"
    if ((Test-Path $RootExe) -and -not (Test-Path $BinExe)) {
        Safe-CopyFile $RootExe $BinExe
    }
    if ((Test-Path $BinExe) -and -not (Test-Path $RootExe)) {
        Safe-CopyFile $BinExe $RootExe
    }

    # Add to User PATH if needed
    $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $PathsToAdd = @($BinDir, $InstallDir)
    $UpdatedPath = $UserPath

    foreach ($P in $PathsToAdd) {
        if (-not (";$UpdatedPath;" -like "*;$P;*")) {
            Write-Host "Adding '$P' to User PATH..." -ForegroundColor Green
            $UpdatedPath = if ([string]::IsNullOrWhiteSpace($UpdatedPath)) { $P } else { "$UpdatedPath;$P" }
        }
    }

    if ($UpdatedPath -ne $UserPath) {
        [Environment]::SetEnvironmentVariable("Path", $UpdatedPath, "User")
    }

    # Update current session PATH so user can immediately use 'dmc'
    if (-not (";$env:Path;" -like "*;$BinDir;*")) {
        $env:Path = "$BinDir;$InstallDir;$env:Path"
    }

    Write-Host "`nDemonic C has been installed successfully!" -ForegroundColor Green
    
    # Test execution
    $DmcCmd = Join-Path $BinDir "dmc.exe"
    if (Test-Path $DmcCmd) {
        $VerOutput = & $DmcCmd --version 2>&1
        Write-Host "Installed: $VerOutput" -ForegroundColor Cyan
    }

    Write-Host @"

Quickstart:
  dmc --version          Check version
  dmc new my-project     Create a new project
  dmc build main.dmc     Compile directly to executable
  dmc test test-files    Run test suite

To get started right away in new terminals, ensure PATH changes have taken effect.
Enjoy programming in Demonic C!
"@ -ForegroundColor White

} finally {
    if (Test-Path $TempZip) {
        Remove-Item -Path $TempZip -Force -ErrorAction SilentlyContinue
    }
}
