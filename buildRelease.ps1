<#
PowerShell script to build the release version of PGPatcher as well as a symbols file.
This script is intended to be run from the command line, not from within an IDE.
#>

param(
    [switch]$NoZip,                # If specified, skip creating the zip file
    [string]$Version,              # --version <PG_VERSION>
    [int]$Prerelease               # --prerelease <PRERELEASE_NUM>
)

# Enable strict mode for safer scripting
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Param validation
$hasVersion = $PSBoundParameters.ContainsKey('Version') -and -not [string]::IsNullOrWhiteSpace($Version)
$hasPrerelease = $PSBoundParameters.ContainsKey('Prerelease') -and $null -ne $Prerelease

# XOR: true when only one is provided
if ($hasVersion -xor $hasPrerelease) {
    throw [System.ArgumentException]::new(
        "Parameters -Version and -Prerelease must be provided together (either specify both, or neither)."
    )
}

# Get the current script directory
$scriptDir = $PSScriptRoot
if (-not $scriptDir) {
    $scriptDir = (Get-Location).Path
}

# Setup folders with absolute paths
$buildDir = Join-Path -Path $scriptDir -ChildPath "buildRelease"
$sourceBinDir = Join-Path -Path $buildDir -ChildPath "bin"
$distDir = Join-Path -Path $scriptDir -ChildPath "dist"
$toolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

function New-LauncherExe {
    param(
        [Parameter(Mandatory = $true)][string]$LauncherPath,
        [Parameter(Mandatory = $true)][string]$TargetRelativePath
    )

    $csc = Get-Command csc -ErrorAction SilentlyContinue
    if (-not $csc) {
        throw "Unable to find csc compiler required to generate launcher executable."
    }

    $sourceFile = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("pgpatcher-launcher-" + [System.Guid]::NewGuid().ToString() + ".cs")

    $launcherSource = @"
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

public static class Launcher {
    private static string QuoteArg(string arg) {
        if (string.IsNullOrEmpty(arg)) {
            return "\"\"";
        }

        if (!arg.Any(ch => char.IsWhiteSpace(ch) || ch == '\"')) {
            return arg;
        }

        var sb = new StringBuilder();
        sb.Append('"');
        int backslashCount = 0;

        foreach (var ch in arg) {
            if (ch == '\\') {
                backslashCount++;
                continue;
            }

            if (ch == '\"') {
                sb.Append('\\', backslashCount * 2 + 1);
                sb.Append('\"');
                backslashCount = 0;
                continue;
            }

            sb.Append('\\', backslashCount);
            backslashCount = 0;
            sb.Append(ch);
        }

        sb.Append('\\', backslashCount * 2);
        sb.Append('"');
        return sb.ToString();
    }

    public static int Main(string[] args) {
        var moduleFileName = Process.GetCurrentProcess().MainModule?.FileName;
        if (string.IsNullOrWhiteSpace(moduleFileName)) {
            Console.Error.WriteLine("Unable to determine launcher executable path.");
            return 1;
        }

        var exeDir = Path.GetDirectoryName(moduleFileName);
        if (string.IsNullOrWhiteSpace(exeDir)) {
            Console.Error.WriteLine("Unable to determine launcher directory.");
            return 1;
        }

        var target = Path.GetFullPath(Path.Combine(exeDir, "$TargetRelativePath"));
        if (!File.Exists(target)) {
            Console.Error.WriteLine("Target executable was not found: " + target);
            return 1;
        }

        var libDir = Path.GetDirectoryName(target) ?? exeDir;
        var psi = new ProcessStartInfo {
            FileName = target,
            WorkingDirectory = exeDir,
            UseShellExecute = false,
            Arguments = string.Join(" ", args.Select(QuoteArg))
        };

        psi.EnvironmentVariables["PATH"] = libDir + ";" + (Environment.GetEnvironmentVariable("PATH") ?? "");

        using (var process = Process.Start(psi)) {
            if (process == null) {
                Console.Error.WriteLine("Failed to start target executable: " + target);
                return 1;
            }

            process.WaitForExit();
            return process.ExitCode;
        }
    }
}
"@

    try {
        Set-Content -Path $sourceFile -Value $launcherSource -Encoding UTF8
        & $csc.Source /nologo /target:exe /out:$LauncherPath $sourceFile
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile launcher executable at $LauncherPath."
        }
    }
    finally {
        if (Test-Path -Path $sourceFile) {
            Remove-Item -Path $sourceFile -Force
        }
    }
}

# Delete build directory if it exists
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir
}

if (Test-Path $distDir) {
    Remove-Item -Recurse -Force $distDir
}

if (-not (Test-Path -Path $buildDir -PathType Container)) {
    New-Item -Path $buildDir -ItemType Directory -Force | Out-Null
}

# Configure PGPatcher
$cmakeArgs = @(
    "-B", $buildDir,
    "-S", ".",
    "-G", "Ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
)

if ($PSBoundParameters.ContainsKey('Version') -and $Version) {
    $cmakeArgs += "-DPG_VERSION=$Version"
}

if ($PSBoundParameters.ContainsKey('Prerelease')) {
    $cmakeArgs += "-DPG_PRERELEASE=$Prerelease"
}

# print command that will be run
Write-Host "Running cmake with arguments: $($cmakeArgs -join ' ')"

cmake @cmakeArgs

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
$libDir = Join-Path -Path $fileDir -ChildPath "lib"
New-Item -Path $tempDir -ItemType Directory -Force | Out-Null
New-Item -Path $fileDir -ItemType Directory -Force | Out-Null
New-Item -Path $libDir -ItemType Directory -Force | Out-Null

try {
    # Copy DLLs, EXEs, JSONs and folders from build/bin
    if (-not (Test-Path -Path $sourceBinDir -PathType Container)) {
        Write-Error "Source directory does not exist: $sourceBinDir"
        exit 1
    }

    Write-Host "Copying files and directories from $sourceBinDir..."

    # Copy DLLs, EXEs, and JSONs
    $allowedExes = @('PGPatcher.exe', 'pgtools.exe')
    Get-ChildItem -Path $sourceBinDir | ForEach-Object {
        # Bool to see if file should be copied
        $copyFile = $false

        # Check if file ends in .dll or .pdb, or is a whitelisted .exe
        if ($_.Name -match '\.dll$' -or $_.Name -match '\.pdb$') {
            $copyFile = $true
        }
        if ($_.Name -match '\.exe$' -and $allowedExes -contains $_.Name) {
            $copyFile = $true
        }

        # Check if file ends in .json (runtimeconfig, deps)
        if ($_.Name -match '\.(runtimeconfig|deps)\.json$') {
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
            $destPath = Join-Path -Path $libDir -ChildPath $_.FullName.Substring($sourceBinDir.Length + 1)
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

    # Generate root launchers so the root folder only contains EXEs
    foreach ($exeName in $allowedExes) {
        $launcherPath = Join-Path -Path $fileDir -ChildPath $exeName
        $targetPath = Join-Path -Path $libDir -ChildPath $exeName
        if (-not (Test-Path -Path $targetPath -PathType Leaf)) {
            throw "Expected target executable was not copied: $targetPath"
        }

        Write-Host "Generating launcher executable: $launcherPath"
        New-LauncherExe -LauncherPath $launcherPath -TargetRelativePath ("lib/" + $exeName)
    }

    # Validate launchers can run with DLLs in lib
    foreach ($exeName in $allowedExes) {
        $launcherPath = Join-Path -Path $fileDir -ChildPath $exeName
        Write-Host "Validating launcher runtime for $exeName"
        $validationOutput = (& $launcherPath --help 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "Launcher validation failed for $exeName with exit code $LASTEXITCODE.`n$validationOutput"
        }
    }

    # Copy all contents of the local "pkg" folder to the root of the zip structure
    $pkgDir = Join-Path -Path $scriptDir -ChildPath "package"
    if (-not (Test-Path -Path $pkgDir -PathType Container)) {
        Write-Error "package directory does not exist: $pkgDir"
        exit 1
    }

    Write-Host "Copying contents of $pkgDir to $tempDir..."

    Copy-Item -Path (Join-Path $pkgDir '*') -Destination $tempDir -Recurse -Force

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
