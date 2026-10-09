# Third-party notices

Tanara is released under the MIT License (see [`LICENSE`](LICENSE)), Copyright (c) RemedIT Hungary Kft.
Tanara's MIT license applies only to Tanara's own source code. The components below remain under
their own licenses. Verbatim license texts are in [`packaging/licenses/`](packaging/licenses/)
(in the Windows packages: the `licenses\` folder next to `tanara.exe`); the index of that folder is
[`packaging/licenses/README.txt`](packaging/licenses/README.txt).

How to read an entry: **name, version, license (SPDX), copyright holder, upstream, license text**.
Versions are those used in the 0.5.x line; the machine-readable list is
[`docs/third-party/inventory.json`](docs/third-party/inventory.json).

Contents

1. [Bundled in all builds](#1-bundled-in-all-builds-vendored-sources-fonts-icons)
2. [Linked libraries](#2-linked-libraries)
3. [Shipped only in Windows packages](#3-shipped-only-in-windows-packages)
4. [Models](#4-models)
5. [External programs on Linux](#5-external-programs-on-linux)
6. [Services and data flow](#6-services-and-data-flow)
7. [Trademarks](#7-trademarks)

## 1. Bundled in all builds (vendored sources, fonts, icons)

The vendored sources are unmodified. Each folder has a `VENDORED.txt` with upstream URL, version and date.

| Component | Version | License (SPDX) | Copyright | Upstream | License text |
|---|---|---|---|---|---|
| kaldi-native-fbank | v1.22.3 (commit `b09e686f`) | Apache-2.0 | Xiaomi Corporation / Fangjun Kuang 2022-2025; Brno University of Technology 2024; the Kaldi authors (Karel Vesely, Petr Motlicek, Saarland University 2009-2011), as in the file headers | https://github.com/csukuangfj/kaldi-native-fbank | `third_party/kaldi-native-fbank/LICENSE`, `licenses/Apache-2.0.txt` |
| miniaudio (with embedded dr_wav, dr_flac, dr_mp3) | 0.11.25 | Unlicense OR MIT-0 | David Reid (and the dr_libs authors) | https://github.com/mackron/miniaudio | statements at the end of `third_party/miniaudio/miniaudio.h` |
| miniz | 3.1.2 | MIT | 2013-2014 RAD Game Tools and Valve Software; 2010-2014 Rich Geldreich and Tenacious Software LLC | https://github.com/richgel999/miniz | `third_party/miniz/LICENSE`, `licenses/MIT-miniz.txt` |
| KISS FFT (float build; Windows builds only, Linux builds link the system package, see section 2) | 131.1.0 | BSD-3-Clause | 2003-2010 Mark Borgerding | https://github.com/mborgerding/kissfft | `third_party/kissfft/COPYING`, `licenses/BSD-3-Clause-kissfft.txt` |
| IBM Plex Sans (Regular, Medium, SemiBold, Bold), IBM Plex Mono (Regular, Medium, SemiBold) | TrueType files in `gui/qml/fonts/` (release not recorded) | OFL-1.1 | 2017 IBM Corp. with Reserved Font Name "Plex" | https://github.com/IBM/plex | `gui/qml/fonts/OFL.txt`, `licenses/OFL-1.1-IBM-Plex.txt` |
| Lucide icons (SVG, from `lucide-static`) | 0.460.0 | ISC (portions MIT, from Feather) | Lucide Contributors 2022; portions Cole Bemis 2013-2022 (Feather) | https://lucide.dev, https://github.com/lucide-icons/lucide | `gui/qml/icons/LICENSE`, `licenses/ISC-Lucide.txt` |

The fonts and icons are compiled into the GUI. The Windows packages carry their license texts in `licenses\`.

## 2. Linked libraries

### Qt 6

- **Version:** 6.11.1 (Windows packages, mingw_64 build); the system Qt 6.11.x on Linux (not shipped by Tanara).
- **Modules used:** Core, Network, Sql (SQLite driver), Widgets, Multimedia, MultimediaWidgets, Qml, Quick, QuickControls2, Svg, and DBus on Linux. Build tools: LinguistTools, moc, rcc, uic, qmltyperegistrar, qmlcachegen, windeployqt. The Test module is used for the unit tests only.
- **License:** LGPL-3.0-only (Qt is available under LGPL-3.0-only OR GPL-3.0-only WITH Qt-GPL-exception-1.0; Tanara uses it under the LGPL-3.0). Texts: `licenses/LGPL-3.0.txt`, `licenses/GPL-3.0.txt`.
- **Copyright:** The Qt Company Ltd. and other contributors. Upstream: https://www.qt.io
- **Notice:** This software uses the Qt toolkit, version 6.11.1, under the GNU Lesser General Public License version 3.
- **Source availability:** the complete corresponding source of Qt 6.11.1 is at https://download.qt.io/archive/qt/6.11/6.11.1/single/qt-everywhere-src-6.11.1.tar.xz (modules also at https://download.qt.io/archive/qt/6.11/6.11.1/submodules/). Tanara does not modify Qt.
- **Relinking:** Qt is linked dynamically (the Qt DLLs and plugins sit next to `tanara.exe` as separate files). You may replace them with your own build of a compatible Qt 6.11.x. Tanara's own source is available under the MIT License at https://github.com/RemedIT-Hungary/Tanara.

### ONNX Runtime

- **Version:** 1.20.1 (Windows packages, `onnxruntime.dll`); the system package on Linux (not shipped; the 1.22.x series was used for development).
- **License:** MIT. **Copyright:** (c) Microsoft Corporation. Upstream: https://github.com/microsoft/onnxruntime
- **License text:** `licenses/MIT-onnxruntime.txt`. The Windows packages also carry the release's own `LICENSE` and `ThirdPartyNotices.txt` as `licenses\onnxruntime-LICENSE.txt` and `licenses\onnxruntime-ThirdPartyNotices.txt`.
- Runs the speaker-embedding models.

### KISS FFT

- On Linux the system `kissfft-float` package is linked (BSD-3-Clause, (c) 2003-2010 Mark Borgerding); on Windows the vendored copy in section 1 is compiled into Tanara. It provides the FFT for kaldi-native-fbank. Upstream: https://github.com/mborgerding/kissfft

### Linux system libraries

The Linux build links the system C and C++ runtime (glibc: LGPL-2.1-or-later; libstdc++: GPL-3.0-or-later WITH GCC-exception-3.1). The audio servers (PipeWire, PulseAudio, ALSA) are loaded dynamically by miniaudio. Tanara does not ship these.

## 3. Shipped only in Windows packages

The Windows zip and installer contain the following binaries in addition to Tanara's own. The package also contains `LICENSE`, this file, and the `licenses\` folder.

### FFmpeg (LGPL build): `ffmpeg.exe`, `ffprobe.exe`

- **What it is:** FFmpeg is a separate program. Tanara starts it as its own process (`QProcess`) to record to Opus, make MP3 mixdowns, and decode imported audio. FFmpeg is not linked into Tanara.
- **Build:** BtbN/FFmpeg-Builds, release `autobuild-2026-09-30-13-08`, file `ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip` (static build). SHA256 `6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432`.
- **Version:** FFmpeg n9.0.2 plus 17 commits of `release/9.0`, commit `2a571b606854520cf89804d8030c8b328e621689`.
- **License:** LGPL-3.0-or-later. The build is configured with `--enable-version3` and without `--enable-gpl` and `--enable-nonfree`; it contains no GPL-only or non-free parts (no x264, no x265). Components inside the build keep their own licenses (for example LAME: LGPL-2.1-or-later; Opus: BSD-3-Clause). The build's own license file is shipped as `licenses\FFmpeg-LICENSE.txt`; the LGPL text is also `licenses\LGPL-3.0.txt`.
- **Copyright:** the FFmpeg developers. FFmpeg is a trademark of Fabrice Bellard, originator of the FFmpeg project.
- **Source:** FFmpeg source of this exact build: https://github.com/FFmpeg/FFmpeg/commit/2a571b606854520cf89804d8030c8b328e621689 (mirror: https://git.ffmpeg.org/ffmpeg.git). The build scripts, which pin the exact version of every library in the build, are at https://github.com/BtbN/FFmpeg-Builds (state of 2026-09-29, commit `6c9aec5fc9a72ec3abedd1fa84db141fa18cf52b`). You can replace `ffmpeg.exe` and `ffprobe.exe` with any other FFmpeg build; Tanara finds them next to the executable.
- Upstream: https://ffmpeg.org

### Qt Multimedia's FFmpeg libraries

`avcodec-61.dll`, `avformat-61.dll`, `avutil-59.dll`, `swresample-5.dll`, `swscale-8.dll` come with Qt Multimedia (FFmpeg 7.1 series, from the library versions), used for audio playback. Qt builds them under LGPL-2.1-or-later. Text: `licenses\LGPL-2.1.txt`. FFmpeg source: https://ffmpeg.org/download.html; Qt's build is described in the Qt source tree (`qtmultimedia`), see the Qt source link in section 2. (Tanara has not verified the exact patch version of these DLLs.)

### Qt's bundled third-party components

The Qt DLLs contain third-party code under permissive licenses: for example PCRE2, double-conversion, zlib, libpng, libjpeg (this software is based in part on the work of the Independent JPEG Group), FreeType, HarfBuzz, md4c, tinycbor and forkfd. The versions and licenses of these components are listed in Qt's documentation for the release in use: https://doc.qt.io/qt-6/licenses-used-in-qt.html.

### MinGW-w64 runtime (GCC 13.1, from the Qt-bundled toolchain)

- `libstdc++-6.dll`, `libgcc_s_seh-1.dll`: GPL-3.0-or-later WITH GCC-exception-3.1 (GCC Runtime Library Exception). Copyright Free Software Foundation, Inc. Texts: `licenses\GPL-3.0.txt`, `licenses\GCC-exception-3.1.txt`. Source: https://gcc.gnu.org (GCC 13.1.0). Under the exception, programs that use the runtime may be distributed under their own license terms.
- `libwinpthread-1.dll` (winpthreads, mingw-w64): MIT-style license, Copyright (c) 2011 mingw-w64 project. Text: `licenses\winpthreads-COPYING.txt`. Upstream: https://www.mingw-w64.org

### Microsoft runtime and graphics components

- **Visual C++ runtime** (`msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`): needed by the MSVC-built `onnxruntime.dll`. Copyright Microsoft Corporation; redistributed under the Microsoft Visual C++ Redistributable terms. These files must come from an official Visual C++ redistributable (the VS / Build Tools "Redist" folder), not from `C:\Windows\System32`; see `packaging/windows/README.md`.
- **`D3Dcompiler_47.dll`** (Microsoft, redistributable component copied by windeployqt) and **`opengl32sw.dll`** (Mesa llvmpipe software OpenGL, Mesa license: MIT, with LLVM parts under Apache-2.0 WITH LLVM-exception). Tanara does not call them directly. Mesa: https://www.mesa3d.org
  Whether `D3Dcompiler_47.dll` and `opengl32sw.dll` can be dropped from the package is an open packaging question; until then they are listed here.

### Installer

The installer is built with Inno Setup 6 (Jordan Russell and Martijn Laan; Inno Setup License). No attribution is required for the installer.

## 4. Models

Tanara downloads or uses these models. The model files are not part of the source repository.

| Model | License | Copyright / origin | Bundled? |
|---|---|---|---|
| CAM++ speaker embedding (`iic/speech_campplus_sv_zh_en_16k-common_advanced`), ONNX export `campplus_sv_zh_en_16k.onnx`, 192-dim | Apache-2.0 (ModelScope license field) | Alibaba / 3D-Speaker authors (https://github.com/modelscope/3D-Speaker); ONNX export from the sherpa-onnx project (k2-fsa, Apache-2.0): https://github.com/k2-fsa/sherpa-onnx/releases/tag/speaker-recongition-models | **Yes, in the Windows packages** (`models\campplus_sv_zh_en_16k.onnx`, 28 MB). On Linux the user downloads it (see README). Text: `licenses/Apache-2.0.txt`. |
| ERes2NetV2 (`iic/speech_eres2netv2_sv_zh-cn_16k-common`), 192-dim | Apache-2.0 | Alibaba / 3D-Speaker authors; sherpa-onnx ONNX export, same release as above | **No.** Downloaded by the user with `tanara-cli voice-models fetch`. |
| WeSpeaker ResNet34-LM (VoxCeleb), `wespeaker_en_voxceleb_resnet34_LM.onnx`, 256-dim | CC-BY-4.0 | WeSpeaker authors (https://github.com/wenet-e2e/wespeaker, model card https://huggingface.co/Wespeaker/wespeaker-voxceleb-resnet34-LM); ONNX export from the sherpa-onnx release above | **No.** Downloaded by the user with `tanara-cli voice-models fetch`. Attribution as CC-BY-4.0 requires: the model is by the WeSpeaker project, licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/); it was converted to ONNX by the sherpa-onnx project and is used unchanged in Tanara. Text: `licenses/CC-BY-4.0.txt`. |
| Google Gemma (default summary model, `google/gemma-4-12b`) | Gemma Terms of Use | Google | **No.** The user downloads it in LM Studio. |
| BAAI bge-m3 (default text-embedding model id `text-embedding-bge-m3`) | MIT | Beijing Academy of Artificial Intelligence | **No.** The user's OpenAI-compatible endpoint serves it, if configured. |

Notes:

- The training-data terms of the speaker models were not examined; the licenses above are the model-level licenses.
- Gemma notice: Gemma is provided under and subject to the Gemma Terms of Use found at https://ai.google.dev/gemma/terms. Use of Gemma is also subject to the Gemma Prohibited Use Policy: https://ai.google.dev/gemma/prohibited_use_policy. Tanara does not distribute Gemma weights.
- Voice prints are computed on your device and stored in `~/.tanara/` (`%USERPROFILE%\.tanara` on Windows). They are biometric personal data; Tanara does not send them anywhere.

## 5. External programs on Linux

On Linux, Tanara does not ship these. It runs the ones installed on your system:

- **FFmpeg** (`ffmpeg`, `ffprobe`): started as external programs. License depends on your distribution's build (LGPL-2.1-or-later or GPL). Upstream: https://ffmpeg.org
- **PipeWire** `pw-dump`: MIT, Copyright PipeWire contributors. Used to find which application is using the microphone. Upstream: https://pipewire.org

## 6. Services and data flow

Tanara has no telemetry and no update check. It contacts only the services you configure:

- **Soniox** (cloud speech-to-text, default STT, your own API key): Tanara **uploads the meeting audio** to Soniox for transcription. Terms: https://soniox.com/company/policies/terms-and-conditions/ - privacy policy: https://soniox.com/company/policies/privacy-policy/
- **OpenAI-compatible endpoints** (summaries, tags, embeddings; for example LM Studio, Ollama, OpenAI): local by default (`http://localhost:1234/v1`). The transcript text goes to the endpoint you enter; if you enter a cloud endpoint, that provider's terms apply.
- **Tanara Cloud** (RemedIT Hungary Kft., optional, `api.tanara.remedit.hu`): transcription, summary and embedding gateway with sign-in. Privacy information (address as set in the app; the final page is not yet confirmed): https://app.tanara.remedit.hu/legal/privacy
- **Model downloads:** `tanara-cli voice-models fetch` downloads from GitHub (sherpa-onnx releases).

## 7. Trademarks

Google Meet, Gemma, Microsoft Teams, Zoom, Discord, Webex, Slack, Skype, Vivaldi, Soniox, LM Studio, Ollama, OpenAI, FFmpeg, Qt and other names are trademarks of their owners. They are used only to name products. Tanara is not affiliated with and not endorsed by them.
