# Build the Lab 3 hardware model on Windows with MSYS2 UCRT64 g++.
# BCrypt (AES-256-GCM) is a Windows system library; -lbcrypt is required.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$cxx = "g++"
if (-not (Get-Command $cxx -ErrorAction SilentlyContinue)) {
    throw "g++ not found in PATH. Install MSYS2 UCRT64 or use CMake."
}

New-Item -ItemType Directory -Force build | Out-Null
& $cxx -std=c++17 -O2 -Wall -Wextra ctm_hw.cpp main.cpp -o build/ctm_hw.exe -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "ctm_hw build failed" }

Write-Host "Done: lab3/hw_model/build/ctm_hw.exe"
