#pragma once
//
// The common, classified error (foundation): what providers, the Tanara Cloud client and the job layer
// exchange instead of their own error types. The job layer (library/jobs/JobErrors) turns it into a
// displayable JobError; the cloud module maps its CloudError onto it (cloud::toError).
//
//  Auth        - the key / sign-in is missing, wrong or revoked (401/403)
//  Quota       - out of balance / keret / spend limit (402, quota 429)
//  Transient   - try again later: network error, timeout, rate limit, maintenance, 5xx upstream
//  Input       - the request itself is wrong: validation, too large, does not fit the model's context
//                (code "context_overflow"), unknown model / address (404)
//  Unsupported - the client / server cannot do it (client too old, no such capability)
//  Service     - the service failed (500, unparseable response)
//  Cancelled   - the user / caller cancelled
#include <QHash>
#include <QMetaType>
#include <QString>

namespace tanara::net { struct HttpExchange; }

namespace tanara {

struct Error {
    enum class Kind { None, Auth, Quota, Transient, Input, Unsupported, Service, Cancelled };

    Kind    kind = Kind::None;
    int     httpStatus = 0;     // 0 = no HTTP response
    QString code;               // machine code: the service's error code, or e.g. "context_overflow"
    QString message;            // human message (the service's, if it gave one in the user's language)
    int     retryAfterSec = 0;  // Retry-After (0 = not given)
    QString requestId;          // the service's request id (support reference), if any
    QString detail;             // technical extra, e.g. the Qt network error text

    bool isError() const { return kind != Kind::None; }

    static Error cancelled();
};

// Stable machine name of a kind ("auth", "quota", ...): logs and tests.
QString errorKindName(Error::Kind kind);

// Classification by HTTP status alone (0 = network error -> Transient).
Error::Kind errorKindForHttpStatus(int httpStatus);

namespace net {
// The error of a failed HTTP exchange: kind from the status, code + message from the body (the
// Soniox {error_type, message}, the OpenAI-compatible {error:{code|type, message}} and the Tanara
// gateway {error:{code, message, request_id}} shapes), Retry-After from the headers. A 2xx exchange
// -> Kind::None.
Error errorFromExchange(const HttpExchange& ex);

// The technical line of a failed HTTP exchange: "HTTP 401 · invalid_api_key · <message>"; for a
// network error (no HTTP response): "network · <Qt error text>".
QString failureDetail(const HttpExchange& ex);
} // namespace net

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Error)
