# Tanara Cloud gateway API — v1 contract

This document is the contract between the open-source Tanara client and the
Tanara Cloud gateway. Both sides follow it. The runnable reference is
`tests/mock-gateway/mock-gateway.mjs`.

## Principles

- The gateway exposes the two interfaces the client already speaks: the Soniox
  async STT API shape and the OpenAI-compatible LLM API shape. Cloud mode is a
  configuration difference in the client, not a new code path.
- The gateway streams audio and text through. It does not store them. It deletes
  the upstream file and transcription after the client fetched the result.
- Prices live in the gateway only. The client never computes a price. It asks
  the gateway for an estimate.

## Headers

| Direction | Header | Value |
|---|---|---|
| request | `Authorization` | `Bearer <api_key>` |
| request | `X-Tanara-Client` | `<semver>/<platform>`, for example `0.2.0/linux` |
| response | `X-Tanara-Min-Client` | the lowest client version the gateway serves |

A client older than `X-Tanara-Min-Client` gets `426` on every call. The client
must check the header on each response and stop cloud calls when it is too old.

## Errors

All errors are JSON: `{"error": {"code": "<code>", "message": "..."}}` plus
code-specific fields.

| HTTP | code | extra fields |
|---|---|---|
| 401 | `unauthorized` | |
| 402 | `insufficient_credit` | `balance`, `needed`, `topup_url` |
| 426 | `client_too_old` | `min_client` |
| 400 | `authorization_pending`, `expired_token`, `invalid_device_code` | (device flow) |

## Auth: device flow

1. `POST /v1/auth/device/code` → `{device_code, user_code, verification_uri, interval, expires_in}`
2. The client opens `verification_uri` in the browser. The user approves there.
3. The client polls `POST /v1/auth/device/token {device_code}` every `interval`
   seconds. It gets `400 authorization_pending` until approval, then
   `{api_key, email}`.
4. The client stores the key in its KeyStore under `tanara.cloud.apiKey`.

## Account

`GET /v1/account` → `{email, balance_credits, dashboard_url}`

## Catalog

`GET /v1/models` → OpenAI list shape. Each model carries a `tanara` object:

```json
{"id": "tanara/stt-accurate", "object": "model", "owned_by": "tanara",
 "tanara": {"kind": "stt", "tier": "accurate", "virtual": true,
            "diarization": true, "languages": null,
            "hidden_for_languages": [], "price": {"per_hour_credits": 100}}}
```

- `kind`: `stt` or `llm`.
- `tier`: `fast`, `accurate`, or `null` for a concrete (Expert) model.
- `virtual`: `true` for `tanara/*` names. The gateway resolves them.
- `diarization`: STT only. When `false`, the client warns for Hungarian and
  falls back to a single speaker label.
- `hidden_for_languages`: the client does not offer the model for these
  languages by default.
- `price`: `per_hour_credits` (STT, per recorded hour) or
  `per_1k_tokens_credits` (LLM). Informational only.

The client has no hard-coded model list.

## Estimate (server side)

`POST /v1/estimate`

```json
{"task": "transcribe" | "summarize" | "both", "duration_ms": 3600000,
 "tracks": 2, "stt_model": "tanara/stt-accurate",
 "llm_model": "tanara/summary-accurate", "language": "hu",
 "transcript_chars": 0}
```

→

```json
{"credits_estimate": 160, "credits_low": 128, "credits_high": 208,
 "balance_credits": 5000, "enough": true,
 "breakdown": [{"item": "stt", "model": "...", "credits": 100},
               {"item": "llm", "model": "...", "credits": 60}]}
```

The gateway estimates from its own statistics. The client shows the numbers
and uses `enough` to allow or warn. The real charge is booked from the actual
upstream cost.

## STT passthrough (Soniox shape)

- `POST /v1/files` (multipart, field `file`) → `{id, size}`
- `POST /v1/transcriptions` → `{id, status}`. The body is the Soniox body
  (`model`, `file_id`, `language_hints`, `enable_speaker_diarization`,
  `context`). `model` accepts a virtual name.
- `GET /v1/transcriptions/{id}` → `{id, status, error_message}`,
  status is `queued`, `processing`, `completed`, or `error`.
- `GET /v1/transcriptions/{id}/transcript` → `{tokens: [{text, start_ms,
  end_ms, confidence, speaker}]}`
- `DELETE /v1/transcriptions/{id}`, `DELETE /v1/files/{id}` → `204`. The
  client calls these after success and after failure. The gateway forwards
  them upstream. They are idempotent.

The charge is booked when the transcription is created. A `402` here means
nothing was booked.

## LLM passthrough (OpenAI shape)

- `POST /v1/chat/completions` with `stream: false`. Virtual model names are
  resolved by the gateway. The response is the OpenAI chat completion object.
- `GET /v1/models` is the catalog above.

## Mock gateway

```
node tests/mock-gateway/mock-gateway.mjs --port 8300 --balance 5000 --auto-approve
```

Test key without login: `tk_test_123`. Flags: `--soniox-key` (real STT
passthrough), `--llm <url>` (real LLM passthrough, canned fallback),
`--min-client` (to test the 426 path), `--balance 0` (to test the 402 path).
