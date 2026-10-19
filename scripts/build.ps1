# Build IridiScope with the MSYS2 mingw64 toolchain.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1
#   powershell ... -File scripts\build.ps1 -Clean
param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Clean,
    [switch]$RequireIio,
    [string]$IioRoot = ""
)

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$mingw = "C:\msys64\mingw64\bin"

if (-not (Test-Path (Join-Path $mingw "g++.exe"))) {
    throw "MSYS2 mingw64 toolchain not found at $mingw. Install MSYS2 + mingw-w64-x86_64-toolchain."
}

$env:PATH = "$mingw;" + $env:PATH
$build = Join-Path $root "build"

if ($Clean) {
    Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
}

$cmakeArgs = @("-S", $root, "-B", $build, "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=$BuildType")
if ($RequireIio) { $cmakeArgs += "-DIRIDISCOPE_REQUIRE_IIO=ON" }
if ($IioRoot) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$IioRoot" }
Write-Host "cmake $($cmakeArgs -join ' ')"
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Built: $(Join-Path $build 'IridiScope.exe')"
