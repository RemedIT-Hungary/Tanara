<#
.SYNOPSIS
  Build the Tanara Windows packages: the tester zip and the Inno Setup installer.
.DESCRIPTION
  The scripted form of the manual steps in packaging/windows/README.md. Steps:
    1. version from CMakeLists.txt (project(VERSION) + TANARA_VERSION_SUFFIX); with -Tag the
       script fails when the tag is not v<version>
    2. configure and build (Release, MinGW 13.1 from Qt, voice-ID on) unless -SkipBuild
    3. dist\ folder: Tanara exes, windeployqt (QML imports, no unused SQL drivers), qoffscreen,
       onnxruntime.dll, VC++ runtime DLLs (see "VC++ runtime" below; never System32),
       LGPL FFmpeg (ffmpeg.exe, ffprobe.exe), the CAM++ model (downloaded, SHA256-checked),
       LICENSE, THIRD_PARTY_NOTICES.md, licenses\
    4. smoke test of dist\ on a clean PATH (unless -NoSmokeTest)
    5. zip, then ISCC (Inno Setup 6). With -SkipInstaller the script stops after the zip (exit 0).
       When ISCC is missing, Chocolatey installs Inno Setup only on CI (CI / GITHUB_ACTIONS set);
       on a developer machine the script stops with a message instead.
  VC++ runtime: only onnxruntime.dll (MSVC-built) needs msvcp140.dll, vcruntime140.dll and
  vcruntime140_1.dll. Tanara's own binaries (MinGW build) do not need the MSVC CRT. Sources, in order:
    a. -CrtDir <folder with the three DLLs> (a Microsoft.VC14x.CRT folder; System32 is rejected)
    b. the Visual Studio / Build Tools Redist folder, found automatically
    c. -DownloadCrt (opt-in): downloads Microsoft's vc_redist.x64.exe into -DepsDir and extracts the
       DLLs WITHOUT installing it (see Get-CrtFromRedist). The URL is a moving target
       (https://aka.ms/vs/17/release/vc_redist.x64.exe), so the SHA256 cannot be pinned; instead the
       Authenticode signature of the exe and of every extracted DLL must be valid and from
       Microsoft. It is not part of the CI path, so CI stays deterministic.
  Qt, MinGW, the ONNX Runtime and the FFmpeg build come from the same places as in
  tools/ci/build-test.ps1 and tools/ci/windows-deps.ps1 (run that script first: it downloads
  ONNX Runtime and FFmpeg into -DepsDir).
.PARAMETER SkipInstaller
  Build, dist, smoke test and zip, then exit 0 without Inno Setup (no ISCC lookup, no Chocolatey).
.PARAMETER DownloadCrt
  Opt-in fallback when no -CrtDir and no Visual Studio Redist folder exists: download and unpack
  Microsoft's vc_redist.x64.exe (signature-checked, not SHA256-pinned).
.EXAMPLE
  powershell -File tools\ci\windows-deps.ps1 -Dest C:\ci-deps
  powershell -File tools\ci\package-windows.ps1 -DepsDir C:\ci-deps -QtDir C:\Qt\6.11.1\mingw_64 -MingwBin C:\Qt\Tools\mingw1310_64\bin
.EXAMPLE
  # laptop without Visual Studio and Inno Setup: zip only, CRT from Microsoft's redistributable
  powershell -File tools\ci\package-windows.ps1 -DepsDir C:\ci-deps -QtDir ... -MingwBin ... -SkipInstaller -DownloadCrt
#>
[CmdletBinding()]
param(
    [string]$Tag = '',
    [string]$BuildDir = 'build',
    [string]$OutDir = '',
    [string]$DepsDir = $(if ($env:RUNNER_TEMP) { Join-Path $env:RUNNER_TEMP 'deps' } else { Join-Path $env:TEMP 'tanara-ci-deps' }),
    [string]$QtDir = $env:QT_ROOT_DIR,
    [string]$MingwBin = $(if ($env:IQTA_TOOLS) { Join-Path $env:IQTA_TOOLS 'mingw1310_64\bin' } else { '' }),
    [string]$CrtDir = '',
    [string]$Iscc = '',
    [int]$Jobs = 4,
    [switch]$SkipBuild,
    [switch]$NoSmokeTest,
    [switch]$SkipInstaller,
    [switch]$DownloadCrt
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutDir) { $OutDir = Join-Path $root 'build-release' }

# --- pinned inputs (keep in step with tools/ci/windows-deps.ps1, packaging/windows/README.md, THIRD_PARTY_NOTICES.md)
$ortName = 'onnxruntime-win-x64-1.20.1'
$ffName  = 'ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0'
$model = @{
    Name = 'campplus_sv_zh_en_16k.onnx'    # CAM++ (3D-Speaker), Apache-2.0, sherpa-onnx export
    Url  = 'https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx'
    Sha  = 'aa3cfc16963a10586a9393f5035d6d6b57e98d358b347f80c2a30bf4f00ceba2'
}
$ort = Join-Path $DepsDir $ortName
$ff  = Join-Path $DepsDir "ffmpeg-lgpl\$ffName"

function Step($m) { Write-Host "== $m" }
function Need($path, $what) { if (-not (Test-Path $path)) { throw "$what not found: $path" } }

# Download Microsoft's vc_redist.x64.exe and extract the three DLLs without installing anything.
# The exe is a WiX Burn bundle: "/layout <dir>" copies its payloads (the MSI packages and their
# cabinets) to a folder and installs nothing; "msiexec /a" (administrative install) then unpacks
# the files of each MSI into a folder tree without registering anything. Best effort: the layout
# of the bundle is Microsoft's, so a failure here ends with the three options in the message.
function Get-CrtFromRedist([string]$work) {
    $want = 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
    $out = Join-Path $work 'crt'
    if ($want | Where-Object { -not (Test-Path (Join-Path $out $_)) }) {
        New-Item -ItemType Directory -Force $work | Out-Null
        $exe = Join-Path $work 'vc_redist.x64.exe'
        Step 'download vc_redist.x64.exe (unpinned URL, signature-checked)'
        Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $exe
        $sig = Get-AuthenticodeSignature $exe
        if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw "vc_redist.x64.exe: bad signature ($($sig.Status))" }
        $layout = Join-Path $work 'layout'
        $admin = Join-Path $work 'admin'
        Remove-Item -Recurse -Force $layout, $admin, $out -ErrorAction SilentlyContinue
        & $exe /layout $layout /quiet /norestart | Out-Null
        $msis = @(Get-ChildItem $layout -Recurse -Filter *.msi -ErrorAction SilentlyContinue)
        if (-not $msis) { throw '"vc_redist.x64.exe /layout" produced no MSI packages' }
        $n = 0
        foreach ($m in $msis) {
            $n++
            $p = Start-Process msiexec.exe -ArgumentList '/a', "`"$($m.FullName)`"", '/qn', "TARGETDIR=`"$admin\$n`"" -Wait -PassThru
            if ($p.ExitCode -ne 0) { Write-Warning "msiexec /a $($m.Name) exited with $($p.ExitCode)" }
        }
        New-Item -ItemType Directory -Force $out | Out-Null
        foreach ($w in $want) {
            $f = Get-ChildItem $admin -Recurse -Filter $w -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $f) { throw "$w not found in the extracted redistributable" }
            $s = Get-AuthenticodeSignature $f.FullName
            if ($s.Status -ne 'Valid' -or $s.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw "$w : bad signature ($($s.Status))" }
            Copy-Item $f.FullName $out
        }
        Remove-Item -Recurse -Force $layout, $admin -ErrorAction SilentlyContinue
    }
    return $out
}

# --- 1. version
$cml = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cml -notmatch '(?m)^project\(tanara VERSION ([0-9][0-9.]*)') { throw 'cannot read project(VERSION) from CMakeLists.txt' }
$num = $Matches[1]
$suffix = ''
if ($cml -match '(?m)^set\(TANARA_VERSION_SUFFIX "([^"]*)"\)') { $suffix = $Matches[1] }
$version = "$num$suffix"
if ($Tag) {
    $t = $Tag -replace '^refs/tags/', ''
    if ($t -ne "v$version") { throw "tag '$t' does not match the CMake version 'v$version'" }
}
Step "Tanara $version"

# --- prerequisites
if (-not $QtDir -or -not (Test-Path "$QtDir\bin")) { throw "Qt folder not found (-QtDir / QT_ROOT_DIR): '$QtDir'" }
if (-not $MingwBin -or -not (Test-Path "$MingwBin\g++.exe")) { throw "MinGW bin folder not found (-MingwBin / IQTA_TOOLS): '$MingwBin'" }
Need "$ort\include" 'ONNX Runtime (run tools\ci\windows-deps.ps1 first)'
Need "$ff\bin\ffmpeg.exe" 'FFmpeg (run tools\ci\windows-deps.ps1 first)'
$env:PATH = "$QtDir\bin;$MingwBin;$env:PATH"
# CMake 3.23 fails at generate time ("Target Qt6::QmlIntegration not found" in tests/): the Qt targets
# found in gui/ are not visible to the sibling folders. 3.24 or newer works; Qt's own CMake
# (C:\Qt\Tools\CMake_64\bin) is fine, put it first on PATH when a standalone, older CMake is installed.
$cmakeVer = (& cmake --version 2>$null | Select-Object -First 1) -replace '[^0-9.]', ''
if (-not $cmakeVer) { throw 'cmake not found on PATH' }
if ([version]$cmakeVer -lt [version]'3.24') { throw "CMake $cmakeVer is too old for this build (3.24 or newer; e.g. C:\Qt\Tools\CMake_64\bin)" }
$build = Join-Path $root $BuildDir

# --- 2. build
if (-not $SkipBuild) {
    Step 'configure and build'
    $qtPath = $QtDir -replace '\\', '/'
    $ortPath = $ort -replace '\\', '/'
    cmake -S $root -B $build -G Ninja "-DCMAKE_PREFIX_PATH=$qtPath" -DCMAKE_BUILD_TYPE=Release `
        -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=$ortPath"
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    cmake --build $build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}
foreach ($e in 'gui\tanara.exe', 'cli\tanara-cli.exe', 'watcher\tanara-watcher.exe') { Need (Join-Path $build $e) 'built program' }

# --- 3. dist folder
Step 'assemble dist'
$dist = Join-Path $OutDir 'dist'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $OutDir, $dist, "$dist\models", "$dist\licenses" | Out-Null
Copy-Item (Join-Path $build 'gui\tanara.exe'), (Join-Path $build 'cli\tanara-cli.exe'), (Join-Path $build 'watcher\tanara-watcher.exe') $dist
& "$QtDir\bin\windeployqt.exe" --release --compiler-runtime --qmldir (Join-Path $root 'gui\qml') `
    --exclude-plugins qsqlibase,qsqlpsql,qsqlmimer,qsqloci,qsqlodbc `
    --dir $dist "$dist\tanara.exe" "$dist\tanara-cli.exe" "$dist\tanara-watcher.exe"
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }
# windeployqt deploys only qwindows; "--qml-shot" (diagnostics, offscreen) needs qoffscreen
Copy-Item "$QtDir\plugins\platforms\qoffscreen.dll" "$dist\platforms"

# never rely on System32\onnxruntime.dll (an older Windows ML copy)
Copy-Item "$ort\lib\onnxruntime.dll" $dist

# VC++ runtime (onnxruntime.dll is MSVC-built): app-local, from a Visual Studio / Build Tools Redist folder
if (-not $CrtDir) {
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
    $cands = foreach ($r in $roots) {
        Get-ChildItem -Path "$r\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -Directory -ErrorAction SilentlyContinue
    }
    $CrtDir = ($cands | Where-Object {
            (Test-Path "$($_.FullName)\msvcp140.dll") -and (Test-Path "$($_.FullName)\vcruntime140.dll") -and (Test-Path "$($_.FullName)\vcruntime140_1.dll")
        } | Sort-Object { $_.FullName } | Select-Object -Last 1).FullName
}
if (-not $CrtDir -and $DownloadCrt) {
    try { $CrtDir = Get-CrtFromRedist (Join-Path $DepsDir 'vc-redist') }
    catch { throw "-DownloadCrt failed: $($_.Exception.Message)`nUse a Visual Studio Redist folder or -CrtDir <folder with the three DLLs> instead." }
}
if (-not $CrtDir) {
    throw @'
VC++ runtime DLLs not found (needed only by onnxruntime.dll; Tanara's own MinGW binaries do not need the MSVC CRT).
Options:
  1. install Visual Studio / Build Tools (...\VC\Redist\MSVC\<ver>\x64\Microsoft.VC14x.CRT is then found automatically)
  2. pass -CrtDir <folder containing msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll> (not System32)
  3. pass -DownloadCrt: downloads Microsoft's vc_redist.x64.exe (unpinned URL, signature-checked) and extracts the DLLs without installing
'@
}
if ($CrtDir -match '(?i)\\Windows\\System32') { throw '-CrtDir must not be System32' }
Write-Host "VC++ runtime from $CrtDir"
Copy-Item "$CrtDir\msvcp140.dll", "$CrtDir\vcruntime140.dll", "$CrtDir\vcruntime140_1.dll" $dist

# LGPL static FFmpeg: the two exes are all that is needed. Check the licence of the build.
$ffver = (& "$ff\bin\ffmpeg.exe" -hide_banner -version) -join "`n"
if ($ffver -match '--enable-gpl' -or $ffver -match '--enable-nonfree') { throw 'FFmpeg build is not LGPL-only' }
if ($ffver -notmatch '--enable-version3') { throw 'unexpected FFmpeg build (no --enable-version3)' }
$ffenc = (& "$ff\bin\ffmpeg.exe" -hide_banner -encoders) -join "`n"
if ($ffenc -notmatch 'libopus' -or $ffenc -notmatch 'libmp3lame') { throw 'FFmpeg lacks libopus or libmp3lame' }
Copy-Item "$ff\bin\ffmpeg.exe", "$ff\bin\ffprobe.exe" $dist

# CAM++ speaker model: download, check SHA256 (licence: Apache-2.0, see THIRD_PARTY_NOTICES.md section 4)
$modelCache = Join-Path $DepsDir "models\$($model.Name)"
New-Item -ItemType Directory -Force (Split-Path $modelCache) | Out-Null
if (-not (Test-Path $modelCache) -or (Get-FileHash $modelCache -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.Sha) {
    Step 'download the CAM++ model'
    Invoke-WebRequest -Uri $model.Url -OutFile $modelCache
}
$h = (Get-FileHash $modelCache -Algorithm SHA256).Hash.ToLowerInvariant()
if ($h -ne $model.Sha) { throw "CAM++ model checksum mismatch (got $h)" }
Copy-Item $modelCache "$dist\models\$($model.Name)"

# licence material: LGPL/Apache/MIT/OFL/BSD terms need the texts to travel with the binaries
Copy-Item (Join-Path $root 'packaging\windows\OLVASSEL.md') $dist
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') $dist
Copy-Item (Join-Path $root 'packaging\licenses\*') "$dist\licenses"
Copy-Item "$ff\LICENSE.txt" "$dist\licenses\FFmpeg-LICENSE.txt"
Copy-Item "$ort\LICENSE" "$dist\licenses\onnxruntime-LICENSE.txt"
Copy-Item "$ort\ThirdPartyNotices.txt" "$dist\licenses\onnxruntime-ThirdPartyNotices.txt"
foreach ($f in 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses\LGPL-3.0.txt', 'licenses\FFmpeg-LICENSE.txt') { Need "$dist\$f" 'package file' }

# --- 4. smoke test on a clean PATH (no Qt, no MinGW, no ffmpeg)
if (-not $NoSmokeTest) {
    Step 'smoke test'
    $savedPath = $env:PATH
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:TANARA_HOME = Join-Path ([IO.Path]::GetTempPath()) ("tanara-pkgtest-" + [guid]::NewGuid())
    try {
        $v = (& "$dist\tanara-cli.exe" --version) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $v -notmatch [regex]::Escape($num)) { throw "tanara-cli --version failed: $v" }
        Write-Host $v
        & "$dist\tanara-cli.exe" paths show | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'tanara-cli paths show failed' }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $shot = Join-Path $OutDir 'smoke.png'
        # tanara.exe is a GUI-subsystem program (WIN32_EXECUTABLE): PowerShell does not wait for it
        # unless its output goes through a pipeline, and the check below would run before the
        # screenshot exists. The pipe makes it wait and sets $LASTEXITCODE.
        & "$dist\tanara.exe" --qml-shot $shot --qml-page Main --size 1280x820 | Out-Host
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $shot)) { throw 'tanara --qml-shot failed' }
        Remove-Item $shot
    } finally {
        $env:PATH = $savedPath
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force $env:TANARA_HOME -ErrorAction SilentlyContinue
        Remove-Item Env:TANARA_HOME -ErrorAction SilentlyContinue
    }
}

# --- 5. zip and installer
Step 'zip'
$zip = Join-Path $OutDir "Tanara-$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($dist, $zip, 'Optimal', $false)

if ($SkipInstaller) {
    $zsha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    "$zsha  $(Split-Path $zip -Leaf)" | Out-File -FilePath "$zip.sha256" -Encoding ascii
    Write-Host ("{0}  {1:N1} MB" -f (Split-Path $zip -Leaf), ((Get-Item $zip).Length / 1MB))
    Step "done (-SkipInstaller: no installer built): $zip"
    exit 0
}

Step 'installer'
if (-not $Iscc) {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $paths = @(
        $(if ($cmd) { $cmd.Source }),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    $Iscc = $paths | Select-Object -First 1
}
if (-not $Iscc) {
    if ($env:GITHUB_ACTIONS -or $env:CI) {
        Write-Host 'Inno Setup 6 not found: installing it with Chocolatey (CI)'
        choco install innosetup --no-progress -y
        $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    } else {
        throw 'Inno Setup 6 (ISCC.exe) not found. Install it (winget install JRSoftware.InnoSetup), pass -Iscc <path>, or pass -SkipInstaller to stop after the zip.'
    }
}
Need $Iscc 'ISCC.exe'
& $Iscc "/DDistDir=$dist" "/DBuildDir=$build" "/DAppVersion=$version" "/DOutputDir=$OutDir" (Join-Path $root 'packaging\windows\tanara.iss')
if ($LASTEXITCODE -ne 0) { throw 'ISCC failed' }
$setup = Join-Path $OutDir "Tanara-$version-win64-setup.exe"
Need $setup 'installer'

foreach ($f in $zip, $setup) {
    $sha = (Get-FileHash $f -Algorithm SHA256).Hash.ToLowerInvariant()
    "$sha  $(Split-Path $f -Leaf)" | Out-File -FilePath "$f.sha256" -Encoding ascii
    Write-Host ("{0}  {1:N1} MB" -f (Split-Path $f -Leaf), ((Get-Item $f).Length / 1MB))
}
Step "done: $OutDir"
