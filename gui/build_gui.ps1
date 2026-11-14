# Manual Qt GUI build for Windows (no qmake/CMake required).
# Tested with Qt 6.11.1 + MinGW 13.1.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$QtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt\6.11.1\mingw_64" }
$QtMingw = if ($env:QT_MINGW) { $env:QT_MINGW } else { "C:\Qt\Tools\mingw1310_64" }
$gxx = Join-Path $QtMingw "bin\g++.exe"
$moc = Join-Path $QtRoot "bin\moc.exe"

if (-not (Test-Path $gxx)) { throw "g++ not found: $gxx. Set QT_MINGW." }
if (-not (Test-Path $moc)) { throw "moc not found: $moc. Set QT_ROOT." }

New-Item -ItemType Directory -Force release | Out-Null

Write-Host "moc..."
& $moc -I. -I../include -I"$QtRoot/include" mainwindow.h -o moc_mainwindow.cpp
& $moc -I. -I../include -I"$QtRoot/include" telemetryplot.h -o moc_telemetryplot.cpp

$sources = @(
    "main.cpp",
    "mainwindow.cpp",
    "telemetryplot.cpp",
    "moc_mainwindow.cpp",
    "moc_telemetryplot.cpp",
    "../src/model_sensor.cpp",
    "../src/sampler.cpp",
    "../src/filters.cpp",
    "../src/acquisition_pipeline.cpp",
    "../src/platform.cpp"
)

$flags = @(
    "-std=c++17", "-O2",
    "-DQT_NO_DEBUG", "-DUNICODE", "-D_UNICODE",
    "-DQT_WIDGETS_LIB", "-DQT_GUI_LIB", "-DQT_CORE_LIB",
    "-I.", "-I../include",
    "-I$QtRoot/include",
    "-I$QtRoot/include/QtCore",
    "-I$QtRoot/include/QtGui",
    "-I$QtRoot/include/QtWidgets"
)

Write-Host "compile and link..."
& $gxx @flags @sources `
    "-L$QtRoot/lib" `
    -lQt6Widgets -lQt6Gui -lQt6Core -lQt6EntryPoint `
    -lwinmm -pthread `
    -o release/cooling_telemetry_gui.exe `
    "-Wl,-subsystem,windows"
if ($LASTEXITCODE -ne 0) { throw "Qt GUI build failed" }

Write-Host "deploy Qt runtime..."
$dlls = @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
    "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll"
)
foreach ($dll in $dlls) {
    Copy-Item (Join-Path $QtRoot "bin/$dll") release -Force
}
New-Item -ItemType Directory -Force release/platforms | Out-Null
foreach ($plugin in @("qwindows.dll", "qminimal.dll", "qoffscreen.dll")) {
    Copy-Item (Join-Path $QtRoot "plugins/platforms/$plugin") release/platforms -Force
}

Write-Host "Done: gui/release/cooling_telemetry_gui.exe"
