<#
.SYNOPSIS
    HorizonEngine Build Environment Bootstrap (Windows)
    Downloads and configures CMake, LLVM/Clang, and Ninja
    into a portable Tools/ directory so the engine can be built
    without a full Visual Studio installation.

.DESCRIPTION
    This script checks for required build tools and downloads missing ones:
      - CMake (portable ZIP)
      - LLVM/Clang (installer, silent)
      - Ninja (portable ZIP)
    
    On Windows, if Visual Studio with C++ workload is already installed,
    MSVC will be preferred. Otherwise Clang + Ninja is used.

.PARAMETER Compiler
    Preferred compiler: 'clang' (default), 'msvc', or 'auto'.
    'auto' uses MSVC if found, otherwise installs Clang.

.PARAMETER ToolsDir
    Directory to install portable tools into.
    Default: <script_dir>/../Tools

.PARAMETER Force
    Re-download all tools even if already present.

.EXAMPLE
    .\bootstrap.ps1
    .\bootstrap.ps1 -Compiler msvc
    .\bootstrap.ps1 -Compiler clang -Force
#>

[CmdletBinding()]
param(
    [ValidateSet('clang', 'msvc', 'auto')]
    [string]$Compiler = 'auto',

    [string]$ToolsDir = '',

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# ── Version Configuration ─────────────────────────────────────────────────
# Update these when newer versions are released.
$CMakeVersion       = '3.31.4'
$LLVMVersion        = '19.1.6'
$NinjaVersion       = '1.12.1'
# ── Download URLs
$CMakeZipUrl        = "https://github.com/Kitware/CMake/releases/download/v${CMakeVersion}/cmake-${CMakeVersion}-windows-x86_64.zip"
$LLVMInstallerUrl   = "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVMVersion}/LLVM-${LLVMVersion}-win64.exe"
$NinjaZipUrl        = "https://github.com/ninja-build/ninja/releases/download/v${NinjaVersion}/ninja-win.zip"

# VS Build Tools (fallback for MSVC)
$VSBuildToolsUrl    = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'

# VC++ Redistributable (bundled with game builds for end-user machines)
$VCRedistUrl        = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'

# Windows SDK standalone installer (needed for Clang without VS)
$WinSdkUrl          = 'https://go.microsoft.com/fwlink/?linkid=2272610'  # Win 11 SDK 10.0.26100

# ── Paths ─────────────────────────────────────────────────────────────────
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$EngineRoot = (Resolve-Path (Join-Path $ScriptDir '..')).Path

if ([string]::IsNullOrEmpty($ToolsDir)) {
    $ToolsDir = Join-Path $EngineRoot 'Tools'
}

$TempDir    = Join-Path $ToolsDir '_temp'
$CMakeDir   = Join-Path $ToolsDir 'cmake'
$LLVMDir    = Join-Path $ToolsDir 'llvm'
$NinjaDir   = Join-Path $ToolsDir 'ninja'

# ── Helper Functions ──────────────────────────────────────────────────────

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "==> $msg" -ForegroundColor Cyan
}

function Write-OK([string]$msg) {
    Write-Host "    [OK] $msg" -ForegroundColor Green
}

function Write-Skip([string]$msg) {
    Write-Host "    [SKIP] $msg" -ForegroundColor Yellow
}

function Write-Err([string]$msg) {
    Write-Host "    [ERROR] $msg" -ForegroundColor Red
}

function Ensure-Dir([string]$path) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
}

function Download-File([string]$url, [string]$dest) {
    Write-Host "    Downloading: $url"
    Write-Host "    To: $dest"

    # Use BITS for large files, WebClient for small ones
    $fileSize = 0
    try {
        $req = [System.Net.WebRequest]::Create($url)
        $req.Method = 'HEAD'
        $req.AllowAutoRedirect = $true
        $req.Timeout = 15000
        $resp = $req.GetResponse()
        $fileSize = $resp.ContentLength
        $resp.Close()
    } catch {
        Write-Host "    [WARN] Could not determine file size (HEAD request failed: $($_.Exception.Message))" -ForegroundColor Yellow
    }

    $maxRetries = 3
    for ($attempt = 1; $attempt -le $maxRetries; $attempt++) {
        try {
            if ($attempt -gt 1) {
                Write-Host "    Retry attempt $attempt of $maxRetries..." -ForegroundColor Yellow
                Start-Sleep -Seconds (2 * $attempt)
            }

            if ($fileSize -gt 10MB) {
                Write-Host "    (Large file: $([math]::Round($fileSize / 1MB, 1)) MB - using background download)"
                Start-BitsTransfer -Source $url -Destination $dest -DisplayName "Downloading..." -ErrorAction Stop
            } else {
                $wc = New-Object System.Net.WebClient
                $wc.Headers.Add('User-Agent', 'HorizonEngine-Bootstrap/1.0')
                $wc.DownloadFile($url, $dest)
            }

            # Verify download succeeded
            if (Test-Path $dest) {
                $dlSize = (Get-Item $dest).Length
                if ($dlSize -gt 0) {
                    return  # Success
                }
                Write-Host "    Downloaded file is empty (0 bytes)." -ForegroundColor Yellow
                Remove-Item $dest -Force -ErrorAction SilentlyContinue
            }
        } catch {
            Write-Host "    Download failed: $($_.Exception.Message)" -ForegroundColor Yellow
            if (Test-Path $dest) { Remove-Item $dest -Force -ErrorAction SilentlyContinue }
        }
    }

    throw "Failed to download $url after $maxRetries attempts."
}

function Test-CommandExists([string]$cmd) {
    $null = Get-Command $cmd -ErrorAction SilentlyContinue
    return $?
}

function Find-MSVC {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    
    $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ([string]::IsNullOrEmpty($vsPath)) { return $null }
    
    $vcToolsDir = Join-Path $vsPath 'VC\Tools\MSVC'
    if (-not (Test-Path $vcToolsDir)) { return $null }
    
    $latest = Get-ChildItem $vcToolsDir -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { return $null }
    
    $cl = Join-Path $latest.FullName 'bin\Hostx64\x64\cl.exe'
    if (Test-Path $cl) {
        $version = & $vswhere -latest -property catalog.productDisplayVersion 2>$null
        return @{
            Path    = $vsPath
            Compiler = $cl
            Version = $version
        }
    }
    return $null
}

# ── Main ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "=============================================" -ForegroundColor White
Write-Host "  HorizonEngine Build Environment Bootstrap" -ForegroundColor White
Write-Host "=============================================" -ForegroundColor White
Write-Host "  Engine Root : $EngineRoot"
Write-Host "  Tools Dir   : $ToolsDir"
Write-Host "  Compiler    : $Compiler"
Write-Host ""

Ensure-Dir $ToolsDir
Ensure-Dir $TempDir

# ── 1. Detect / Install CMake ─────────────────────────────────────────────
Write-Step "Checking CMake..."

$cmakeExe = Join-Path $CMakeDir 'bin\cmake.exe'
$cmakeBundled = $false

if (-not $Force -and (Test-Path $cmakeExe)) {
    $ver = & $cmakeExe --version 2>$null | Select-Object -First 1
    Write-OK "Bundled CMake found: $ver"
    $cmakeBundled = $true
}

# Always ensure a local/bundled cmake exists in Tools/cmake/ so the editor
# can find it reliably.  A system cmake on PATH is not sufficient because
# the editor process may not inherit the same PATH (e.g. VS Developer Shell
# adds cmake, but a normal user session does not).
if (-not $cmakeBundled -or $Force) {
    if (Test-CommandExists 'cmake') {
        $sysVer = cmake --version 2>$null | Select-Object -First 1
        Write-Host "    System CMake detected ($sysVer), but installing portable copy for editor..."
    } else {
        Write-Host "    No system CMake found."
    }
    Write-Host "    Installing CMake ${CMakeVersion} (portable)..."
    $zipFile = Join-Path $TempDir "cmake-${CMakeVersion}.zip"
    Download-File $CMakeZipUrl $zipFile
    
    if (Test-Path $CMakeDir) { Remove-Item $CMakeDir -Recurse -Force }
    Ensure-Dir $CMakeDir
    
    Write-Host "    Extracting..."
    Expand-Archive -Path $zipFile -DestinationPath $TempDir -Force
    $extracted = Get-ChildItem (Join-Path $TempDir "cmake-${CMakeVersion}*") -Directory | Select-Object -First 1
    
    # Move contents up one level
    Get-ChildItem $extracted.FullName | Move-Item -Destination $CMakeDir -Force
    
    $cmakeExe = Join-Path $CMakeDir 'bin\cmake.exe'
    if (Test-Path $cmakeExe) {
        $ver = & $cmakeExe --version 2>$null | Select-Object -First 1
        Write-OK "CMake installed: $ver"

        # Strip unnecessary files to reduce size (~115 MB -> ~18 MB)
        Write-Host "    Stripping unnecessary files..."
        $stripBins = @('cmake-gui.exe', 'ctest.exe', 'cpack.exe', 'cmcldeps.exe')
        foreach ($bin in $stripBins) {
            $p = Join-Path $CMakeDir "bin\$bin"
            if (Test-Path $p) { Remove-Item $p -Force }
        }
        $stripDirs = @(
            (Join-Path $CMakeDir 'doc'),
            (Join-Path $CMakeDir 'man')
        )
        # Remove editor integrations and help from share/
        $shareDir = Join-Path $CMakeDir 'share'
        if (Test-Path $shareDir) {
            $stripDirs += @(
                (Join-Path $shareDir 'aclocal'),
                (Join-Path $shareDir 'bash-completion'),
                (Join-Path $shareDir 'emacs'),
                (Join-Path $shareDir 'vim')
            )
            # Remove Help/ from the cmake-X.Y directory (docs, not needed at runtime)
            Get-ChildItem $shareDir -Directory -Filter 'cmake-*' | ForEach-Object {
                $helpDir = Join-Path $_.FullName 'Help'
                if (Test-Path $helpDir) { $stripDirs += $helpDir }
            }
        }
        foreach ($d in $stripDirs) {
            if (Test-Path $d) { Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue }
        }

        # Report final size
        $finalSize = (Get-ChildItem $CMakeDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
        Write-OK "CMake stripped to $([math]::Round($finalSize / 1MB, 1)) MB"
    } else {
        Write-Err "CMake installation failed!"
        exit 1
    }
}

# ── 2. Detect / Install Ninja ─────────────────────────────────────────────
Write-Step "Checking Ninja..."

$ninjaExe = Join-Path $NinjaDir 'ninja.exe'
$ninjaBundled = $false

if (-not $Force -and (Test-Path $ninjaExe)) {
    $ver = & $ninjaExe --version 2>$null
    Write-OK "Bundled Ninja found: $ver"
    $ninjaBundled = $true
}

if (-not $ninjaBundled -or $Force) {
    if (Test-CommandExists 'ninja') {
        $sysVer = ninja --version 2>$null
        Write-Host "    System Ninja detected ($sysVer), but installing portable copy for editor..."
    } else {
        Write-Host "    No system Ninja found."
    }
    Write-Host "    Installing Ninja ${NinjaVersion}..."
    $zipFile = Join-Path $TempDir "ninja-${NinjaVersion}.zip"
    Download-File $NinjaZipUrl $zipFile
    
    if (Test-Path $NinjaDir) { Remove-Item $NinjaDir -Recurse -Force }
    Ensure-Dir $NinjaDir
    
    Expand-Archive -Path $zipFile -DestinationPath $NinjaDir -Force
    
    if (Test-Path $ninjaExe) {
        Write-OK "Ninja installed: $(& $ninjaExe --version 2>$null)"
    } else {
        Write-Err "Ninja installation failed!"
        exit 1
    }
}

# ── 3. Detect / Install Compiler ──────────────────────────────────────────
Write-Step "Checking C++ Compiler..."

$msvcInfo = Find-MSVC
$useClang = $false
$useMSVC = $false

if ($Compiler -eq 'auto') {
    if ($msvcInfo) {
        Write-OK "MSVC found: Visual Studio $($msvcInfo.Version) at $($msvcInfo.Path)"
        $useMSVC = $true
    } else {
        Write-Host "    MSVC not found, will install Clang."
        $useClang = $true
    }
}
elseif ($Compiler -eq 'msvc') {
    if ($msvcInfo) {
        Write-OK "MSVC found: Visual Studio $($msvcInfo.Version)"
        $useMSVC = $true
    } else {
        Write-Host "    MSVC not found. Installing Visual Studio Build Tools..."
        
        $installerPath = Join-Path $TempDir 'vs_BuildTools.exe'
        Download-File $VSBuildToolsUrl $installerPath
        
        Write-Host "    Running silent install (this may take 10-30 minutes)..."
        Write-Host "    Installing: MSVC C++ tools, Windows SDK, CMake tools"
        
        $installArgs = @(
            '--quiet',
            '--wait',
            '--norestart',
            '--nocache',
            '--add', 'Microsoft.VisualStudio.Workload.VCTools',
            '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621',
            '--includeRecommended'
        )
        
        $proc = Start-Process -FilePath $installerPath -ArgumentList $installArgs -Wait -PassThru
        
        if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
            # Recheck
            $msvcInfo = Find-MSVC
            if ($msvcInfo) {
                Write-OK "MSVC Build Tools installed: $($msvcInfo.Version)"
                $useMSVC = $true
            } else {
                Write-Err "MSVC installation completed but compiler not found!"
                Write-Host "    A reboot may be required (exit code: $($proc.ExitCode))."
                exit 1
            }
        } else {
            Write-Err "MSVC Build Tools installation failed (exit code: $($proc.ExitCode))."
            exit 1
        }
    }
}
elseif ($Compiler -eq 'clang') {
    $useClang = $true
}

if ($useClang) {
    $clangExe = Join-Path $LLVMDir 'bin\clang++.exe'
    $clangFound = $false
    
    if (-not $Force -and (Test-Path $clangExe)) {
        $ver = & $clangExe --version 2>$null | Select-Object -First 1
        Write-OK "Bundled Clang found: $ver"
        $clangFound = $true
    }
    elseif (-not $Force -and (Test-CommandExists 'clang++')) {
        $ver = clang++ --version 2>$null | Select-Object -First 1
        Write-OK "System Clang found: $ver"
        $clangExe = (Get-Command clang++).Source
        $clangFound = $true
    }
    
    if (-not $clangFound -or $Force) {
        Write-Host "    Installing LLVM/Clang ${LLVMVersion} (silent installer)..."
        $installerPath = Join-Path $TempDir "LLVM-${LLVMVersion}.exe"

        # Download with error handling
        try {
            Download-File $LLVMInstallerUrl $installerPath
        } catch {
            Write-Err "Failed to download LLVM installer from: $LLVMInstallerUrl"
            Write-Err "Error: $_"
            Write-Host "    Check your internet connection or download manually from:" -ForegroundColor Yellow
            Write-Host "    https://github.com/llvm/llvm-project/releases/tag/llvmorg-${LLVMVersion}" -ForegroundColor Yellow
            exit 1
        }

        if (-not (Test-Path $installerPath)) {
            Write-Err "LLVM installer not found after download: $installerPath"
            exit 1
        }

        $installerSize = (Get-Item $installerPath).Length
        Write-Host "    Downloaded installer: $([math]::Round($installerSize / 1MB, 1)) MB"

        if ($installerSize -lt 1MB) {
            Write-Err "LLVM installer file is too small ($([math]::Round($installerSize / 1KB, 1)) KB) - download likely incomplete or corrupt."
            Write-Host "    Deleting corrupt file and retrying may help. Use -Force to re-download." -ForegroundColor Yellow
            Remove-Item $installerPath -Force -ErrorAction SilentlyContinue
            exit 1
        }

        $installDir = $LLVMDir
        Write-Host "    Target directory: $installDir"

        # Delete leftover files from previous install
        if (Test-Path $installDir) {
            Write-Host "    Removing leftover files in $installDir..."
            Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue
            if (Test-Path $installDir) {
                Start-Sleep -Seconds 2
                Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

        # Ensure parent directory exists
        $installParent = Split-Path $installDir -Parent
        if (-not (Test-Path $installParent)) {
            New-Item -ItemType Directory -Path $installParent -Force | Out-Null
        }

        # ── Extract NSIS installer with 7-Zip (bypasses all NSIS issues) ──
        # The NSIS .exe is just a self-extracting archive.  By extracting it
        # directly with 7-Zip we avoid: registry conflicts, path-with-spaces
        # failures, exit-code-2 uninstall errors, and admin requirements.
        $7zaExe = Join-Path $TempDir '7za.exe'
        $7zaUrl = 'https://www.7-zip.org/a/7zr.exe'

        if (-not (Test-Path $7zaExe)) {
            Write-Host "    Downloading 7-Zip extractor (~1 MB)..."
            try {
                Download-File $7zaUrl $7zaExe
            } catch {
                Write-Err "Failed to download 7-Zip extractor."
                Write-Host "    Fallback: download 7zr.exe manually from https://www.7-zip.org" -ForegroundColor Yellow
                exit 1
            }
        }

        Write-Host "    Extracting LLVM from installer (this may take a few minutes)..."
        Ensure-Dir $installDir

        # 7zr.exe can extract NSIS installers: 7zr x <file> -o<dir>
        $extractArgs = "x `"$installerPath`" -o`"$installDir`" -y"
        try {
            $proc = Start-Process -FilePath $7zaExe -ArgumentList $extractArgs -Wait -PassThru -NoNewWindow
        } catch {
            Write-Err "Failed to run 7-Zip extraction."
            Write-Err "Error: $_"
            exit 1
        }

        if ($proc.ExitCode -ne 0) {
            Write-Err "7-Zip extraction failed (exit code: $($proc.ExitCode))."
            Write-Host "    The NSIS installer may use a format 7zr.exe cannot handle." -ForegroundColor Yellow
            Write-Host "    Trying NSIS silent install as fallback..." -ForegroundColor Yellow

            # Fallback: run NSIS installer directly
            $proc2 = Start-Process -FilePath $installerPath -ArgumentList "/S /D=$installDir" -Wait -PassThru
            if ($proc2.ExitCode -ne 0) {
                Write-Err "NSIS installer also failed (exit code: $($proc2.ExitCode))."
                Write-Host "    Troubleshooting:" -ForegroundColor Yellow
                Write-Host "      1. Try: .\bootstrap.ps1 -Compiler clang -Force" -ForegroundColor Yellow
                Write-Host "      2. Try: .\bootstrap.ps1 -ToolsDir C:\Tools -Compiler clang" -ForegroundColor Yellow
                Write-Host "      3. Use MSVC instead: .\bootstrap.ps1 -Compiler msvc" -ForegroundColor Yellow
                Write-Host "      4. Download manually: https://github.com/llvm/llvm-project/releases/tag/llvmorg-${LLVMVersion}" -ForegroundColor Yellow
                exit 1
            }
        }

        # The NSIS extraction puts files under $`INSTDIR/ subfolder - flatten if needed
        $extractedBin = Join-Path $installDir 'bin\clang++.exe'
        if (-not (Test-Path $extractedBin)) {
            # Check for $INSTDIR or similar NSIS subfolder
            $subDirs = Get-ChildItem $installDir -Directory -ErrorAction SilentlyContinue
            foreach ($sub in $subDirs) {
                $candidate = Join-Path $sub.FullName 'bin\clang++.exe'
                if (Test-Path $candidate) {
                    Write-Host "    Moving extracted files from $($sub.Name)/ to install root..."
                    Get-ChildItem $sub.FullName | Move-Item -Destination $installDir -Force
                    Remove-Item $sub.FullName -Recurse -Force -ErrorAction SilentlyContinue
                    break
                }
            }
        }

        $clangBinary = Join-Path $installDir 'bin\clang++.exe'

        if (Test-Path $clangBinary) {
            $clangExe = $clangBinary
            $ver = & $clangExe --version 2>$null | Select-Object -First 1
            Write-OK "LLVM/Clang installed: $ver"

            # Verify essential files
            $essentialFiles = @('bin\clang.exe', 'bin\clang++.exe', 'bin\lld-link.exe', 'lib\clang')
            $missingFiles = @()
            foreach ($f in $essentialFiles) {
                $fullPath = Join-Path $installDir $f
                if (-not (Test-Path $fullPath)) { $missingFiles += $f }
            }
            if ($missingFiles.Count -gt 0) {
                Write-Host "    [WARNING] Installation may be incomplete. Missing:" -ForegroundColor Yellow
                foreach ($mf in $missingFiles) {
                    Write-Host "      - $mf" -ForegroundColor Yellow
                }
            }
        } else {
            Write-Err "LLVM/Clang installation failed!"
            Write-Host "    Expected binary not found: $clangBinary" -ForegroundColor Red

            if (Test-Path $installDir) {
                $items = Get-ChildItem $installDir -ErrorAction SilentlyContinue
                if ($items) {
                    Write-Host "    Contents of ${installDir}:" -ForegroundColor Yellow
                    foreach ($item in $items) {
                        Write-Host "      $($item.Name)" -ForegroundColor Yellow
                    }
                }
            } else {
                Write-Host "    Install directory was not created." -ForegroundColor Yellow
            }

            try {
                $drive = (Split-Path $installDir -Qualifier)
                $disk = Get-PSDrive ($drive -replace ':','')
                $freeGB = [math]::Round($disk.Free / 1GB, 1)
                Write-Host "    Available disk space on ${drive}: ${freeGB} GB" -ForegroundColor Yellow
            } catch {}

            Write-Host ""
            Write-Host "    Troubleshooting:" -ForegroundColor Yellow
            Write-Host "      1. Try: .\bootstrap.ps1 -ToolsDir C:\Tools -Compiler clang" -ForegroundColor Yellow
            Write-Host "      2. Use MSVC instead: .\bootstrap.ps1 -Compiler msvc" -ForegroundColor Yellow
            Write-Host "      3. Download manually: https://github.com/llvm/llvm-project/releases/tag/llvmorg-${LLVMVersion}" -ForegroundColor Yellow
            Write-Host ""
            exit 1
        }
    }
    
    # Clang on Windows needs Windows SDK headers + MSVC CRT headers.
    # Check if available, auto-install if missing.
    $winSdkInclude = $null
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    if (Test-Path $sdkRoot) {
        $latestSdk = Get-ChildItem $sdkRoot -Directory | Where-Object { $_.Name -match '^\d+\.' } | Sort-Object Name -Descending | Select-Object -First 1
        if ($latestSdk) {
            $winSdkInclude = $latestSdk.FullName
        }
    }

    if (-not $winSdkInclude) {
        Write-Host "    Windows SDK not found. Installing..." -ForegroundColor Yellow

        $sdkInstalled = $false

        # Method 1: winget (fastest, no temp download)
        if (-not $sdkInstalled -and (Test-CommandExists 'winget')) {
            Write-Host "    Trying winget..."
            $wgResult = Start-Process -FilePath 'winget' -ArgumentList 'install','--id','Microsoft.WindowsSDK.10.0.26100','--silent','--accept-package-agreements','--accept-source-agreements' -Wait -PassThru -NoNewWindow 2>$null
            if ($wgResult -and ($wgResult.ExitCode -eq 0)) {
                Write-OK "Windows SDK installed via winget."
                $sdkInstalled = $true
            }
        }

        # Method 2: Standalone SDK installer (silent)
        if (-not $sdkInstalled) {
            Write-Host "    Downloading Windows SDK installer..."
            $sdkInstallerPath = Join-Path $TempDir 'winsdksetup.exe'
            try {
                Download-File $WinSdkUrl $sdkInstallerPath

                Write-Host "    Running silent Windows SDK install (this may take 5-10 minutes)..."
                $sdkArgs = @(
                    '/quiet',
                    '/norestart',
                    '/features', 'OptionId.DesktopCPPx64'
                )
                $sdkProc = Start-Process -FilePath $sdkInstallerPath -ArgumentList $sdkArgs -Wait -PassThru

                if ($sdkProc.ExitCode -eq 0) {
                    Write-OK "Windows SDK installed."
                    $sdkInstalled = $true
                } else {
                    Write-Host "    SDK installer exit code: $($sdkProc.ExitCode)" -ForegroundColor Yellow
                }
            } catch {
                Write-Host "    SDK download/install failed: $_" -ForegroundColor Yellow
            }
        }

        if (-not $sdkInstalled) {
            Write-Host ""
            Write-Host "    [WARNING] Windows SDK could not be installed automatically." -ForegroundColor Yellow
            Write-Host "    Please install manually:" -ForegroundColor Yellow
            Write-Host "      Option A: https://developer.microsoft.com/windows/downloads/windows-sdk/" -ForegroundColor Yellow
            Write-Host "      Option B: winget install Microsoft.WindowsSDK.10.0.26100" -ForegroundColor Yellow
            Write-Host "      Option C: .\bootstrap.ps1 -Compiler msvc  (installs VS Build Tools with SDK)" -ForegroundColor Yellow
            Write-Host ""
        }
    } else {
        Write-OK "Windows SDK found: $($latestSdk.Name)"
    }
}

# ── 4. Download VC++ Redistributable (for game distribution) ──────────────
Write-Step "Checking VC++ Redistributable for game distribution..."

$VCRedistPath = Join-Path $ToolsDir 'vc_redist.x64.exe'
if ((Test-Path $VCRedistPath) -and -not $Force) {
    Write-Skip "vc_redist.x64.exe already present."
} else {
    Write-Host "    Downloading VC++ Redistributable (~24 MB)..."
    Download-File $VCRedistUrl $VCRedistPath
    if (Test-Path $VCRedistPath) {
        Write-OK "vc_redist.x64.exe downloaded (for bundling with game builds)."
    } else {
        Write-Host "    [WARN] Download failed. Game builds can still download it automatically." -ForegroundColor Yellow
        Write-Host "    [WARN] Manual download: https://aka.ms/vs/17/release/vc_redist.x64.exe" -ForegroundColor Yellow
    }
}

# ── 5. Clean up temp files
Write-Step "Cleaning up..."
if (Test-Path $TempDir) {
    Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-OK "Temp files removed."
}

# ── 6. Generate environment script
Write-Step "Generating environment setup..."

$envScript = Join-Path $ToolsDir 'env.ps1'
$envBatch  = Join-Path $ToolsDir 'env.bat'

# PowerShell env script
$psContent = @"
# HorizonEngine Build Environment
# Source this script: . .\Tools\env.ps1

`$ToolsRoot = Split-Path -Parent `$MyInvocation.MyCommand.Definition
`$EngineRoot = Split-Path -Parent `$ToolsRoot

# CMake
`$cmakeBin = Join-Path `$ToolsRoot 'cmake\bin'
if (Test-Path `$cmakeBin) { `$env:PATH = "`$cmakeBin;`$env:PATH" }

# Ninja
`$ninjaBin = Join-Path `$ToolsRoot 'ninja'
if (Test-Path `$ninjaBin) { `$env:PATH = "`$ninjaBin;`$env:PATH" }

# LLVM/Clang
`$llvmBin = Join-Path `$ToolsRoot 'llvm\bin'
if (Test-Path `$llvmBin) { `$env:PATH = "`$llvmBin;`$env:PATH" }

Write-Host
"@
Set-Content -Path $envScript -Value $psContent -Encoding UTF8

# CMD/Batch env script
$batContent = @"
@echo off
REM HorizonEngine Build Environment
REM Call this script: call Tools\env.bat

set "TOOLS_ROOT=%~dp0"
set "ENGINE_ROOT=%TOOLS_ROOT%.."

if exist "%TOOLS_ROOT%cmake\bin" set "PATH=%TOOLS_ROOT%cmake\bin;%PATH%"
if exist "%TOOLS_ROOT%ninja" set "PATH=%TOOLS_ROOT%ninja;%PATH%"
if exist "%TOOLS_ROOT%llvm\bin" set "PATH=%TOOLS_ROOT%llvm\bin;%PATH%"
echo
"@
Set-Content -Path $envBatch -Value $batContent -Encoding ASCII

Write-OK "Generated Tools\env.ps1 and Tools\env.bat"

# ── 7. Summary ────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "=============================================" -ForegroundColor Green
Write-Host "  Bootstrap Complete!" -ForegroundColor Green
Write-Host "=============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Tools directory: $ToolsDir" -ForegroundColor White

$cmakeVer = ''
if (Test-Path (Join-Path $CMakeDir 'bin\cmake.exe')) {
    $cmakeVer = & (Join-Path $CMakeDir 'bin\cmake.exe') --version 2>$null | Select-Object -First 1
    Write-Host "  CMake:    $cmakeVer" -ForegroundColor White
}

$ninjaVer = ''
if (Test-Path (Join-Path $NinjaDir 'ninja.exe')) {
    $ninjaVer = & (Join-Path $NinjaDir 'ninja.exe') --version 2>$null
    Write-Host "  Ninja:    $ninjaVer" -ForegroundColor White
}

if ($useMSVC -and $msvcInfo) {
    Write-Host "  Compiler: MSVC $($msvcInfo.Version)" -ForegroundColor White
}
if ($useClang) {
    $clangBin = Join-Path $LLVMDir 'bin\clang++.exe'
    if (Test-Path $clangBin) {
        $cv = & $clangBin --version 2>$null | Select-Object -First 1
        Write-Host "  Compiler: $cv" -ForegroundColor White
    }
}

Write-Host ""
Write-Host "  To build the engine:" -ForegroundColor Yellow
Write-Host "    .\build.bat              (uses detected/installed tools)" -ForegroundColor White
Write-Host ""
Write-Host "  Or manually:" -ForegroundColor Yellow
Write-Host "    . .\Tools\env.ps1        (load environment)" -ForegroundColor White

if ($useClang) {
    Write-Host "    cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -B build -S ." -ForegroundColor White
} else {
    Write-Host "    cmake -G Ninja -B build -S ." -ForegroundColor White
}
Write-Host "    cmake --build build --config Release" -ForegroundColor White
Write-Host ""
