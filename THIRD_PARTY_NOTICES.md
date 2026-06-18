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

## Linked at build/runtime

- **Qt 6** — LGPL v3 (open-source edition). Dynamically linked; Qt remains
  replaceable by the user. Upstream: https://www.qt.io
- **ONNX Runtime** — MIT License. Used to run the speaker-embedding model.
  Upstream: https://github.com/microsoft/onnxruntime
- **KISS FFT** — BSD-3-Clause. Used by kaldi-native-fbank for the FFT.
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
