#pragma once
//
// SonioxReference: the `client_reference_id` Tanara attaches to every Soniox request
// (POST /v1/files and POST /v1/transcriptions).
//
// Purpose: cost correlation after the run. With an own Soniox key, the user's Soniox usage log
// (GET /v1/usage-logs: client_reference_id, model, input_audio_duration_ms, cost_usd per request)
// can be matched to a meeting / processing run / track, so the actual cost is known instead of an
// estimate.
//
// Tanara Cloud: the same field goes to the gateway, which stores it in its job record and echoes it,
// but NEVER forwards it upstream (the client string is untrusted input). Toward Soniox the gateway
// sends its own `tc:<gateway_job_uuid>` reference. Both ids stay unambiguous: the client's in the
// gateway's metadata, the gateway's toward Soniox (docs/cloud-gateway-api.yaml, createTranscription).
//
// Format (version 1):  tanara:1:<meetingId>:<jobId>:<trackId>
//  - any part may be empty ("tanara:1:<meetingId>::<trackId>");
//  - a part never contains ':' (the builder replaces it with '_');
//  - at most 256 characters (Soniox's maxLength; UUIDs never reach it, the cap is defensive).
#include <QString>

namespace tanara::providers {

inline constexpr int kClientReferenceMaxLength = 256;

// The reference of one request. All three parts empty -> an empty string (nothing is sent).
QString buildClientReference(const QString& meetingId, const QString& jobId, const QString& trackId);

struct ClientReference {
    QString meetingId;
    QString jobId;
    QString trackId;
    bool    valid = false;   // a "tanara:1:..." string with exactly 5 ':'-separated parts
};

// The inverse of buildClientReference (e.g. for matching usage-log lines). Anything else: valid=false.
ClientReference parseClientReference(const QString& reference);

} // namespace tanara::providers
