# Build libiio's stable 0.x API with native USB + network backends on Windows.
# Prerequisites in MSYS2 MINGW64:
# pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-libusb mingw-w64-x86_64-libxml2 mingw-w64-x86_64-zstd git
param([string]$Mingw = "C:\msys64\mingw64")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root "build-deps"
$source = Join-Path $deps "libiio-v0.26"
$build = Join-Path $deps "libiio-build"
$install = Join-Path $deps "iio"
$env:PATH = (Join-Path $Mingw "bin") + ";" + $env:PATH
$env:PKG_CONFIG_PATH = (Join-Path $Mingw "lib\pkgconfig")
New-Item -ItemType Directory -Force $deps | Out-Null
if (-not (Test-Path (Join-Path $source "CMakeLists.txt"))) {
    git clone --depth 1 --branch v0.26 https://github.com/analogdevicesinc/libiio.git $source
    if ($LASTEXITCODE -ne 0) { throw "libiio clone failed" }
}
# Pin the source used for this fork, even if the local checkout was changed.
$revision = (git -C $source rev-parse HEAD).Trim()
if ($revision -ne "a0eca0d2bf10326506fb762f0eec14255b27bef5") { throw "Unexpected libiio v0.26 revision: $revision" }
cmake -S $source -B $build -G Ninja "-DCMAKE_BUILD_TYPE=Release" `
    "-DCMAKE_INSTALL_PREFIX=$install" "-DCMAKE_PREFIX_PATH=$Mingw" `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DWITH_TESTS=OFF -DWITH_EXAMPLES=OFF `
    -DWITH_IIOD=OFF -DWITH_USB_BACKEND=ON -DWITH_NETWORK_BACKEND=ON -DWITH_ZSTD=ON
if ($LASTEXITCODE -ne 0) { throw "libiio configure failed" }
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "libiio build failed" }
cmake --install $build
if ($LASTEXITCODE -ne 0) { throw "libiio install failed" }
Write-Host "Next: scripts\build.ps1 -RequireIio -IioRoot $install"
