<#
PowerShell script to build the release version of PGPatcher as well as a symbols file.
This script is intended to be run from the command line, not from within an IDE.
#>

param(
    [switch]$NoZip  # If specified, skip creating the zip file
)

# Enable strict mode for safer scripting
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Get the current script directory
$scriptDir = $PSScriptRoot
if (-not $scriptDir) {
    $scriptDir = (Get-Location).Path
}

# Setup folders with absolute paths
$buildDir = Join-Path -Path $scriptDir -ChildPath "buildRelease"
if (-not (Test-Path -Path $buildDir -PathType Container)) {
    New-Item -Path $buildDir -ItemType Directory -Force | Out-Null
}

$sourceBinDir = Join-Path -Path $buildDir -ChildPath "bin"
$installDir = Join-Path -Path $scriptDir -ChildPath "install"
$distDir = Join-Path -Path $scriptDir -ChildPath "dist"
$toolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

# Delete build directory if it exists
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir
}

# Configure PGPatcher
cmake -B $buildDir -S . -G Ninja `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DCMAKE_INSTALL_PREFIX="$installDir" `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build and install
cmake --build $buildDir

# Create distribution zip file
# Ensure dist directory exists
if (-not (Test-Path -Path $distDir -PathType Container)) {
    New-Item -Path $distDir -ItemType Directory -Force | Out-Null
}

# Define zip file path
$zipFile = Join-Path -Path $distDir -ChildPath "PGPatcher.zip"

# Create a temporary directory to collect the files
$tempDir = Join-Path -Path $distDir -ChildPath "PGPatcher"
$fileDir = Join-Path -Path $tempDir -ChildPath "PGPatcher"
New-Item -Path $tempDir -ItemType Directory -Force | Out-Null
New-Item -Path $fileDir -ItemType Directory -Force | Out-Null

try {
    # Copy DLLs, EXEs, JSONs and folders from build/bin
    if (-not (Test-Path -Path $sourceBinDir -PathType Container)) {
        Write-Error "Source directory does not exist: $sourceBinDir"
        exit 1
    }

    Write-Host "Copying files and directories from $sourceBinDir..."

    # Copy DLLs, EXEs, and JSONs
    Get-ChildItem -Path $sourceBinDir | ForEach-Object {
        # Bool to see if file should be copied
        $copyFile = $false

        # Check if file ends in .dll
        if ($_.Name -match '\.dll$' -or $_.Name -match '\.exe$' -or $_.Name -match '\.pdb$') {
            $copyFile = $true
        }

        # Check if file is "PGMutagen.runtimeconfig.json"
        if ($_.Name -eq 'PGMutagen.runtimeconfig.json') {
            $copyFile = $true
        }

        # Check if is folder and name is "assets"
        if ($_.PSIsContainer -and $_.Name -eq 'assets') {
            $copyFile = $true
        }

        if ($_.PSIsContainer -and $_.Name -eq 'resources') {
            $copyFile = $true
        }

        # Check if is folder and name is "cshaders"
        if ($_.PSIsContainer -and $_.Name -eq 'cshaders') {
            $copyFile = $true
        }

        # Check if is folder and name is "runtimes"
        if ($_.PSIsContainer -and $_.Name -eq 'runtimes') {
            $copyFile = $true
        }

        # Copy file if the conditions are met
        if ($copyFile) {
            $destPath = Join-Path -Path $fileDir -ChildPath $_.FullName.Substring($sourceBinDir.Length + 1)
            $destDir = Split-Path -Path $destPath -Parent

            # Create destination directory if it doesn't exist
            if (-not (Test-Path -Path $destDir -PathType Container)) {
                New-Item -Path $destDir -ItemType Directory -Force | Out-Null
            }

            # Copy the file
            Write-Host "Copying file: $($_.FullName) to $destPath"
            Copy-Item -Path $_.FullName -Destination $destPath -Recurse -Force
        }
    }

    # Create blank meshes folder at root of zip
    $meshesDir = Join-Path -Path $tempDir -ChildPath "meshes"
    Write-Host "Creating blank meshes folder: $meshesDir"
    New-Item -Path $meshesDir -ItemType Directory -Force | Out-Null

    if (-not $NoZip) {
        # Create the zip file
        Write-Host "Creating zip file: $zipFile"
        if (Test-Path -Path $zipFile) {
            Remove-Item -Path $zipFile -Force
        }

        # Custom encoder to fix linux paths
        Add-Type -AssemblyName System.Text.Encoding
        Add-Type -AssemblyName System.IO.Compression.FileSystem

        class FixedEncoder : System.Text.UTF8Encoding {
            FixedEncoder() : base($true) { }

            [byte[]] GetBytes([string] $s) {
                $s = $s.Replace("\\", "/");
                return ([System.Text.UTF8Encoding]$this).GetBytes($s);
            }
        }

        # Create zip file
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::CreateFromDirectory($tempDir, $zipFile, [System.IO.Compression.CompressionLevel]::Optimal, $false, [FixedEncoder]::new())

        Write-Host "Zip file created successfully: $zipFile"
    }
}
catch {
    Write-Error "An error occurred: $_"
    exit 1
}
finally {
    if (-not $NoZip) {
        # Clean up the temporary directory
        if (Test-Path -Path $tempDir) {
            Remove-Item -Path $tempDir -Recurse -Force
        }
    }
}
