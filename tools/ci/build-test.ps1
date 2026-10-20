<#
.SYNOPSIS
  Configure, build and test Tanara on Windows (MinGW from Qt, Ninja, headless ctest).
.DESCRIPTION
  Needs Qt 6.11.1 (mingw_64), the Qt MinGW 13.1 tool, Ninja and CMake. Qt and MinGW are found
  from the parameters, or from the variables that jurplel/install-qt-action sets
  (QT_ROOT_DIR, IQTA_TOOLS). ONNXRUNTIME_ROOT_DIR comes from windows-deps.ps1. ffmpeg, ffprobe
  and node should be on PATH, otherwise the tests that need them skip (QSKIP).
.EXAMPLE
  powershell -File tools\ci\build-test.ps1 -QtDir C:\Qt\6.11.1\mingw_64 -MingwBin C:\Qt\Tools\mingw1310_64\bin
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$QtDir = $env:QT_ROOT_DIR,
    [string]$MingwBin = $(if ($env:IQTA_TOOLS) { Join-Path $env:IQTA_TOOLS 'mingw1310_64\bin' } else { '' }),
    [string]$OnnxRoot = $env:ONNXRUNTIME_ROOT_DIR,
    [int]$Jobs = 4,
    [string[]]$ExtraCMakeArgs = @()
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')

if (-not $QtDir -or -not (Test-Path "$QtDir\bin")) { throw "Qt folder not found (-QtDir / QT_ROOT_DIR): '$QtDir'" }
if (-not $MingwBin -or -not (Test-Path "$MingwBin\g++.exe")) { throw "MinGW bin folder not found (-MingwBin / IQTA_TOOLS): '$MingwBin'" }
if (-not $OnnxRoot -or -not (Test-Path "$OnnxRoot\include")) { throw "ONNX Runtime folder not found (-OnnxRoot / ONNXRUNTIME_ROOT_DIR): '$OnnxRoot'" }

# Qt and MinGW DLLs must be on PATH for the build and for running the tests.
$env:PATH = "$QtDir\bin;$MingwBin;$env:PATH"
$qtPath = $QtDir -replace '\\', '/'
$ortPath = $OnnxRoot -replace '\\', '/'

$build = Join-Path $root $BuildDir
cmake -S $root -B $build -G Ninja "-DCMAKE_PREFIX_PATH=$qtPath" -DCMAKE_BUILD_TYPE=Release `
    -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=$ortPath" @ExtraCMakeArgs
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
cmake --build $build -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

# Sandbox: tests must never touch real user data.
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:TANARA_HOME = Join-Path ([IO.Path]::GetTempPath()) ("tanara-ctest-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force $env:TANARA_HOME | Out-Null
try {
    ctest --test-dir $build -j $Jobs --output-on-failure
    $status = $LASTEXITCODE
} finally {
    Remove-Item -Recurse -Force $env:TANARA_HOME -ErrorAction SilentlyContinue
}
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    $log = Join-Path $build 'Testing\Temporary\LastTest.log'
    & python (Join-Path $PSScriptRoot 'test-summary.py') $log
    & python (Join-Path $PSScriptRoot 'qskip-summary.py') $log
}
exit $status
