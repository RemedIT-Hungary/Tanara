# Third-party notices

Tanara is released under the MIT License (see `LICENSE`). It uses the following
third-party components, each under its own license. Tanara's MIT license applies
only to Tanara's own source code; the components below remain under their
respective licenses.

## Bundled (compiled into Tanara)

- **kaldi-native-fbank** — Apache License 2.0.
  Vendored source under `third_party/kaldi-native-fbank/` (see its `LICENSE`).
  Used for kaldi-compatible filterbank features in the speaker-embedding pipeline.
  Upstream: https://github.com/csukuangfj/kaldi-native-fbank

- **miniaudio** — public domain (MIT-0 alternative).
  Single-header audio capture, vendored under `third_party/miniaudio/`.
  Upstream: https://github.com/mackron/miniaudio

- **miniz** 3.1.2 — MIT License.
  Copyright 2013-2014 RAD Game Tools and Valve Software; Copyright 2010-2014 Rich Geldreich
  and Tenacious Software LLC. Vendored source under `third_party/miniz/` (see its `LICENSE`),
  unmodified. Used to write and read meeting archives (`*.tanara.zip`).
  Upstream: https://github.com/richgel999/miniz

- **IBM Plex Sans / IBM Plex Mono** — SIL Open Font License 1.1.
  Copyright © 2017 IBM Corp. with Reserved Font Name "Plex". TrueType files (Sans
  Regular / Medium / SemiBold / Bold, Mono Regular / Medium / SemiBold) are embedded in
  the GUI from `gui/qml/fonts/` (license text: `gui/qml/fonts/OFL.txt`), unmodified.
  Upstream: https://github.com/IBM/plex

- **Lucide icons** — ISC License (portions MIT, from Feather).
  SVG icons from `lucide-static` 0.460.0 are embedded in the GUI from `gui/qml/icons/`
  (license text: `gui/qml/icons/LICENSE`), recoloured at runtime.
  Upstream: https://lucide.dev — https://github.com/lucide-icons/lucide

## Linked at build/runtime

- **Qt 6** — LGPL v3 (open-source edition). Dynamically linked; Qt remains
  replaceable by the user. Upstream: https://www.qt.io
- **ONNX Runtime** — MIT License. Used to run the speaker-embedding model.
  Upstream: https://github.com/microsoft/onnxruntime
- **KISS FFT** — BSD-3-Clause. Used by kaldi-native-fbank for the FFT. On Linux it
  is linked from the system `kissfft-float` package; on Windows the float build is
  vendored as source under `third_party/kissfft/` and compiled into Tanara.
  Upstream: https://github.com/mborgerding/kissfft

## External programs / services (not linked)

- **FFmpeg** — invoked as an external command-line program (via `QProcess`) for
  audio encoding/decoding; it is **not** linked into Tanara. Install separately.
  Tanara does not redistribute FFmpeg. Upstream: https://ffmpeg.org
- **Soniox** — cloud speech-to-text API (optional, requires your own API key).
- **OpenAI-compatible LLM endpoint** (e.g. LM Studio, Ollama) — used locally for
  summaries; not bundled.

## Models (downloaded separately, not in this repository)

- **3D-Speaker CAM++ speaker-embedding model** (ONNX) — Apache License 2.0.
  Downloaded by the user into `~/.tanara/models/`. Distributed via the
  sherpa-onnx model releases. Upstream: https://github.com/modelscope/3D-Speaker
