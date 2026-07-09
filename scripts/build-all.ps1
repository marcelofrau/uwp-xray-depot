param(
    [string]$VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    [string]$BuildType = "Release"
)

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$ErrorActionPreference = "Stop"

Write-Output "=== build-all.ps1 ==="
Write-Output "Repo root: $RepoRoot"

# 1. Build Lua 5.4
Write-Output ""
Write-Output "---[ Step 1/2: Build Lua 5.4 ]---"
& "$PSScriptRoot\build-lua.ps1" -VcVars $VcVars -BuildType $BuildType
if ($LASTEXITCODE -ne 0) { throw "Lua build failed" }

# 2. Build xray-sock
Write-Output ""
Write-Output "---[ Step 2/2: Build xray-sock ]---"
& "$PSScriptRoot\build-xray-sock.ps1" -VcVars $VcVars -BuildType $BuildType
if ($LASTEXITCODE -ne 0) { throw "xray-sock build failed" }

Write-Output ""
Write-Output "=== build-all.ps1 complete ==="
Write-Output "Output:"
Write-Output "  $RepoRoot\x64\lib\lua5.4.lib"
Write-Output "  $RepoRoot\x64\lib\xray-sock.lib"
Write-Output "  $RepoRoot\x64\include\lua\"
Write-Output "  $RepoRoot\x64\include\xray\"
