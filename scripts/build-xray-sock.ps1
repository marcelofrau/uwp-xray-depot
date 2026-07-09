param(
    [string]$VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    [string]$BuildType = "Release"
)

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$SrcDir = "$RepoRoot\src"
$BuildDir = "$RepoRoot\build\xray-sock"
$OutLib = "$RepoRoot\x64\lib\xray-sock.lib"
$OutInclude = "$RepoRoot\x64\include\xray"

Write-Output "=== build-xray-sock.ps1 ==="
Write-Output "Repo root: $RepoRoot"

# Load MSVC x64 environment
cmd /c """$VcVars"" && set" | ForEach-Object {
    if ($_ -match '^(\w+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

# Verify MSVC
$clPath = (Get-Command cl.exe -ErrorAction Stop).Source
Write-Output "cl.exe: $clPath"

# Build with CMake
Write-Output "Configuring xray-sock..."
Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
$cmakeCfg = cmake -S $SrcDir -B $BuildDir -A x64 2>&1
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

Write-Output "Building xray-sock..."
$cmakeBuild = cmake --build $BuildDir --config $BuildType 2>&1
if ($LASTEXITCODE -ne 0) { Write-Error "CMake build failed"; exit 1 }

# Copy output
Write-Output "Copying lib to $OutLib..."
New-Item -ItemType Directory -Path (Split-Path $OutLib) -Force | Out-Null
Copy-Item "$BuildDir\$BuildType\xray-sock.lib" $OutLib -Force

Write-Output "Headers already in $OutInclude\ (xray-sock.hpp, inspector.hpp)"

# Cleanup build dir
Remove-Item -Path "$RepoRoot\build" -Recurse -Force -ErrorAction SilentlyContinue | Out-Null

Write-Output "=== xray-sock build complete ==="
Write-Output "  LIB: $OutLib"
Write-Output "  Header: $OutInclude\xray-sock.hpp"
