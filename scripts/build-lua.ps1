param(
    [string]$VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$LuaSrc = "$RepoRoot\external\lua"
$OutLib = "$RepoRoot\x64\lib\lua5.4.lib"
$OutInclude = "$RepoRoot\x64\include\lua"

Write-Output "=== build-lua.ps1 ==="
Write-Output "Repo root: $RepoRoot"

# Load MSVC x64 environment
Write-Output "Loading MSVC x64 env from: $VcVars"
cmd /c """$VcVars"" && set" | ForEach-Object {
    if ($_ -match '^(\w+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

# Verify cl.exe is now MSVC
$clPath = (Get-Command cl.exe -ErrorAction Stop).Source
Write-Output "cl.exe: $clPath"
$clVer = cmd /c """$VcVars"" && cl.exe 2>&1" | Select-String -Pattern "Microsoft"
if (-not $clVer) {
    Write-Error "MSVC cl.exe not found. Check VcVars path."
    exit 1
}
Write-Output "MSVC version: $($clVer.Line)"

# Compile each Lua C file to separate obj
$cFiles = Get-ChildItem "$LuaSrc\*.c" -Exclude "lua.c","ltests.c","onelua.c"
$objDir = "$RepoRoot\x64\lib\lua_objs"
New-Item -ItemType Directory -Path $objDir -Force | Out-Null

Write-Output "Compiling $($cFiles.Count) Lua source files..."
$objFiles = @()
foreach ($f in $cFiles) {
    $obj = "$objDir\$($f.BaseName).obj"
    $objFiles += $obj
    $clArgs = @(
        "/c"
        "/O2"
        "/MD"
        "/DWIN32"
        "/D_CRT_SECURE_NO_WARNINGS"
        "/Fo$obj"
        $f.FullName
    )
    & cl.exe $clArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed: $($f.Name)"; exit 1 }
}

# Static lib
Write-Output "Creating $OutLib..."
& lib.exe "/out:$OutLib" $objFiles
if ($LASTEXITCODE -ne 0) { Write-Error "lib.exe failed"; exit 1 }

# Cleanup obj files
Remove-Item -Path $objDir -Recurse -Force

# Copy headers
Write-Output "Copying headers to $OutInclude..."
New-Item -ItemType Directory -Path $OutInclude -Force | Out-Null
@("lua.h","luaconf.h","lualib.h","lauxlib.h") | ForEach-Object {
    Copy-Item "$LuaSrc\$_" "$OutInclude\$_" -Force
}

Write-Output "=== Lua 5.4.7 build complete ==="
Write-Output "  LIB: $OutLib"
Write-Output "  Headers: $OutInclude"
