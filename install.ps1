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

    # Extract
    Write-Host "Extracting files..." -ForegroundColor Cyan
    Expand-Archive -Path $TempZip -DestinationPath $InstallDir -Force

    # Flatten if archive contained a top-level dmc folder
    $NestedDmc = Join-Path $InstallDir "dmc"
    if (Test-Path (Join-Path $NestedDmc "bin")) {
        Copy-Item -Path "$NestedDmc\*" -Destination $InstallDir -Recurse -Force
        Remove-Item -Path $NestedDmc -Recurse -Force
    }

    # Ensure bin directory exists
    $BinDir = Join-Path $InstallDir "bin"
    if (-not (Test-Path $BinDir)) {
        New-Item -Path $BinDir -ItemType Directory -Force | Out-Null
    }

    # Check for dmc.exe in InstallDir vs InstallDir\bin
    $RootExe = Join-Path $InstallDir "dmc.exe"
    $BinExe = Join-Path $BinDir "dmc.exe"
    if ((Test-Path $RootExe) -and -not (Test-Path $BinExe)) {
        Copy-Item -Path $RootExe -Destination $BinExe -Force
    }
    if ((Test-Path $BinExe) -and -not (Test-Path $RootExe)) {
        Copy-Item -Path $BinExe -Destination $RootExe -Force
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
