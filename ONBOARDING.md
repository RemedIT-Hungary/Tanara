# Onboarding

From a fresh clone to a meeting that gets recorded, transcribed, split by speaker and summarized, all on your own machine.

## 1. Build the app

Follow [docs/BUILD.md](docs/BUILD.md) for your platform (Linux, Windows or macOS). You get `tanara` (GUI), `tanara-watcher` (tray app) and `tanara-cli`.

## 2. Start the local services

Tanara itself only records and handles speakers. Transcription and summarization go to servers you run. [compose.yaml](compose.yaml) provides them on Tanara's default ports:

| Service | Used for | Address | In compose |
|---|---|---|---|
| Whisper server ([speaches](https://github.com/speaches-ai/speaches)) | Transcription | `http://localhost:8000/v1` | `whisper` (always on) |
| OpenAI-compatible LLM ([Ollama](https://ollama.com)) | Summary, tag suggestions, embeddings | `http://localhost:1234/v1` | `ollama` (`llm` profile) |

You need Docker (or Podman) with Compose.

```sh
docker compose up -d                  # Whisper only
docker compose --profile llm up -d    # Whisper + Ollama
```

### Download the models

The containers start empty. Models go into named volumes, so you download them once.

```sh
# Whisper (about 3 GB; for a faster, weaker model use Systran/faster-whisper-small)
curl -X POST http://localhost:8000/v1/models/Systran/faster-whisper-large-v3

# LLM and embedding model (llm profile only)
docker compose exec ollama ollama pull gemma3:12b
docker compose exec ollama ollama pull bge-m3
```

Check that they are loaded:

```sh
curl http://localhost:8000/v1/models
curl http://localhost:1234/v1/models
```

### macOS and the LLM

Docker on macOS cannot use the Apple GPU, so a 12B model in a container runs slowly on the CPU. Skip the `llm` profile and run [LM Studio](https://lmstudio.ai) natively instead. Its server listens on `localhost:1234` by default, which is exactly what Tanara expects. Tanara also detects LM Studio and can reload the model with a larger context when a long meeting needs it. The Whisper container works fine on macOS, on the CPU.

## 3. Configure Tanara

Open **Settings** and set each step to **Local**:

1. **Transcription:** provider *Whisper (OpenAI-compatible)*, URL `http://localhost:8000/v1`, model `Systran/faster-whisper-large-v3`. Leave the language empty for auto-detection, or set it (for example `hu`).
2. **Speaker separation:** local. On first use Tanara downloads the pyannote segmentation and speaker-embedding ONNX models and verifies them with SHA-256. This step needs no container.
3. **Speaker recognition:** always local. It needs nothing extra.
4. **Summary / tag suggestions:** provider *OpenAI-compatible*, URL `http://localhost:1234/v1`, model `gemma3:12b` (Ollama) or the model ID shown in LM Studio. No API key is needed.
5. **Embeddings** (optional): URL `http://localhost:1234/v1`, model `bge-m3` (Ollama) or `text-embedding-bge-m3` (LM Studio).

Use the connection test next to each provider. It tells you when the server is unreachable or the model is missing.

## 4. First meeting

1. Start `tanara-watcher`. It notices calls and offers to record them, or you can start a recording from the GUI.
2. Stop the recording, then run processing: transcription, speaker separation, recognition.
3. Fix speakers in **To review**. Each decision trains the voiceprints.
4. Generate the summary.

## Troubleshooting

- **Transcription times out on long meetings:** the CPU image is slow with `large-v3`. Try a smaller model, or on Linux with an NVIDIA GPU switch to the `latest-cuda` image (see the comments in `compose.yaml`).
- **"Does not fit the context" during summary:** with Ollama, raise the context (`OLLAMA_CONTEXT_LENGTH` in the `ollama` service's environment). With LM Studio, Tanara offers to reload the model itself.
- **Port already in use:** change the left side of the port mapping in `compose.yaml` and the URL in Settings.
- **Logs:** `docker compose logs -f whisper` and `docker compose logs -f ollama`.
