# Build without CMake: MSYS2 UCRT64 g++ is expected in PATH.
# Creates build/ and produces ctm_acq.exe and ctm_tests.exe.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$cxx = "g++"
if (-not (Get-Command $cxx -ErrorAction SilentlyContinue)) {
    throw "g++ not found in PATH. Install MSYS2 UCRT64 or use CMake."
}

New-Item -ItemType Directory -Force build | Out-Null

$core = @(
    "src/model_sensor.cpp",
    "src/sampler.cpp",
    "src/filters.cpp",
    "src/acquisition_pipeline.cpp",
    "src/csv_writer.cpp",
    "src/platform.cpp"
)

Write-Host "Building ctm_acq.exe..."
& $cxx -std=c++17 -O2 -Wall -Wextra -Iinclude $core "src/main.cpp" -o "build/ctm_acq.exe" -lwinmm -pthread
if ($LASTEXITCODE -ne 0) { throw "ctm_acq build failed" }

Write-Host "Building ctm_tests.exe..."
& $cxx -std=c++17 -O2 -Wall -Wextra -Iinclude $core "tests/test_main.cpp" -o "build/ctm_tests.exe" -lwinmm -pthread
if ($LASTEXITCODE -ne 0) { throw "ctm_tests build failed" }

Write-Host "Done: build/ctm_acq.exe, build/ctm_tests.exe"
