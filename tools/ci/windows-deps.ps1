<#
.SYNOPSIS
  Download the pinned ONNX Runtime and LGPL FFmpeg for the Windows build, with SHA256 checks.
.DESCRIPTION
  Versions match packaging/windows/README.md. Unpacks to <Dest>\onnxruntime-win-x64-1.20.1 and
  <Dest>\ffmpeg-lgpl\<build folder>. On GitHub Actions it also exports ONNXRUNTIME_ROOT_DIR
  (GITHUB_ENV) and adds the FFmpeg bin folder to PATH (GITHUB_PATH). Elsewhere it prints them.
  -WhatIf is a dry run: it prints what would be downloaded and checked, and changes nothing.
.EXAMPLE
  powershell -File tools\ci\windows-deps.ps1 -Dest C:\ci-deps
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Dest = $(if ($env:RUNNER_TEMP) { Join-Path $env:RUNNER_TEMP 'deps' } else { Join-Path $env:TEMP 'tanara-ci-deps' })
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$items = @(
    @{
        Name = 'ONNX Runtime 1.20.1'
        Url  = 'https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip'
        Sha  = '78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581'
        File = 'onnxruntime.zip'
        Out  = $Dest
        Check = 'onnxruntime-win-x64-1.20.1\include\onnxruntime_cxx_api.h'
    },
    @{
        Name = 'FFmpeg n9.0.2 LGPL static (BtbN)'
        Url  = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip'
        Sha  = '6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432'
        File = 'ffmpeg-lgpl.zip'
        Out  = (Join-Path $Dest 'ffmpeg-lgpl')
        Check = 'ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0\bin\ffmpeg.exe'
    }
)

if ($PSCmdlet.ShouldProcess($Dest, 'create folder')) { New-Item -ItemType Directory -Force $Dest | Out-Null }

foreach ($it in $items) {
    $zip = Join-Path $Dest $it.File
    $marker = Join-Path $it.Out $it.Check
    if (Test-Path $marker) { Write-Host "$($it.Name): already unpacked"; continue }
    if (-not $PSCmdlet.ShouldProcess($it.Url, "download $($it.Name) and verify SHA256 $($it.Sha)")) { continue }
    Write-Host "Downloading $($it.Name)"
    Invoke-WebRequest -Uri $it.Url -OutFile $zip
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $it.Sha) { throw "$($it.Name): checksum mismatch (got $hash, expected $($it.Sha))" }
    Expand-Archive $zip $it.Out -Force
    Remove-Item $zip
    if (-not (Test-Path $marker)) { throw "$($it.Name): expected file missing after unpack: $marker" }
}

$ort = Join-Path $Dest 'onnxruntime-win-x64-1.20.1'
$ffBin = Join-Path $Dest 'ffmpeg-lgpl\ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0\bin'
if ($WhatIfPreference) {
    Write-Host "WhatIf: would set ONNXRUNTIME_ROOT_DIR=$ort and add $ffBin to PATH"
    return
}
if ($env:GITHUB_ENV)  { "ONNXRUNTIME_ROOT_DIR=$ort" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
if ($env:GITHUB_PATH) { $ffBin | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8 }
Write-Host "ONNXRUNTIME_ROOT_DIR=$ort"
Write-Host "FFmpeg bin: $ffBin"
