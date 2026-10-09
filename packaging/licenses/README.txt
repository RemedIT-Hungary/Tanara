Tanara - license texts of third-party components
=================================================

Tanara itself is MIT-licensed (see ../../LICENSE, or LICENSE in the package root).
This folder holds the verbatim license texts of the third-party components Tanara
uses or ships. The full list of components (versions, copyright holders, upstream
URLs, source availability) is in THIRD_PARTY_NOTICES.md.

File                          License                    Applies to
----------------------------  -------------------------  ------------------------------------------
LGPL-3.0.txt                  GNU LGPL v3                Qt 6 (Windows package: the Qt DLLs and plugins);
                                                         the FFmpeg LGPL build (ffmpeg.exe, ffprobe.exe,
                                                         built with --enable-version3)
GPL-3.0.txt                   GNU GPL v3                 Referenced by LGPL-3.0 (it is "the GNU GPL");
                                                         Qt is dual-licensed LGPL-3.0 / GPL-3.0;
                                                         libstdc++ / libgcc (with the exception below)
LGPL-2.1.txt                  GNU LGPL v2.1              FFmpeg libraries bundled with Qt Multimedia
                                                         (avcodec, avformat, avutil, swresample, swscale)
GCC-exception-3.1.txt         GCC Runtime Library        libstdc++-6.dll, libgcc_s_seh-1.dll (MinGW-w64
                              Exception 3.1              runtime, together with GPL-3.0.txt)
winpthreads-COPYING.txt       MIT-style (mingw-w64)      libwinpthread-1.dll
Apache-2.0.txt                Apache License 2.0         kaldi-native-fbank; the CAM++ and ERes2NetV2
                                                         speaker models (3D-Speaker); sherpa-onnx ONNX
                                                         exports of those models
MIT-onnxruntime.txt           MIT                        ONNX Runtime (Copyright (c) Microsoft Corporation)
MIT-miniz.txt                 MIT                        miniz
BSD-3-Clause-kissfft.txt      BSD-3-Clause               KISS FFT (Copyright (c) 2003-2010 Mark Borgerding)
OFL-1.1-IBM-Plex.txt          SIL Open Font License 1.1  IBM Plex Sans / IBM Plex Mono (embedded fonts)
ISC-Lucide.txt                ISC (portions MIT)         Lucide icons (embedded SVGs)
CC-BY-4.0.txt                 Creative Commons BY 4.0    WeSpeaker ResNet34-LM speaker model. NOT bundled:
                                                         Tanara downloads it only when the user runs
                                                         "tanara-cli voice-models fetch".

miniaudio is public domain / MIT-0; its license statement is at the end of
third_party/miniaudio/miniaudio.h and needs no separate file.

Additional files added to the Windows package by the packaging steps (they come from the
upstream archives, so they always match the shipped binaries; see packaging/windows/README.md):

  FFmpeg-LICENSE.txt                   LICENSE.txt of the FFmpeg LGPL build (BtbN)
  FFmpeg-README-build.txt              short build information of that FFmpeg build
  onnxruntime-LICENSE.txt              LICENSE of the ONNX Runtime release
  onnxruntime-ThirdPartyNotices.txt    ThirdPartyNotices.txt of the ONNX Runtime release
  Qt-LICENSES-README.txt               pointer to Qt's third-party license list (see THIRD_PARTY_NOTICES.md)
