#pragma once
//
// VoiceModelDownloader: downloads the speaker models (ONNX) from VoiceModelSpec::downloadUrl to
// <modelsDir>/<fileName>. The shared downloader of the CLI (`voice-models fetch`) and the GUI
// (Settings > Speakers, Getting started).
//
//  - asynchronous (QNetworkAccessManager on the caller's thread, no own event loop): does not block
//    the GUI thread; several models can download at once (by id);
//  - the target file appears only after a successful, size-checked download (it is written as
//    <target>.part and renamed; on error, cancel or size mismatch no half-written file stays);
//  - SHA-256: when the spec pins one, the downloaded file is hashed while it streams. A mismatch is a
//    WARNING, not a hard error: the file is kept as <target>.mismatch (the registry does not see it),
//    finished(id, false, ...) comes with Failure::ShaMismatch, and the user may OVERRIDE
//    (acceptMismatch: "use it anyway"): the file moves to <target> and the actual hash is recorded in
//    <target>.accepted-sha256. From then on verifyFile() says Unverified (usable, flagged) for that exact
//    file (whatever its size: a test build of the model may differ in size too); a different file with
//    another hash is a mismatch again;
//  - companion files (VoiceModelSpec::companions, e.g. the LICENSE) go into the main file's folder first;
//  - does NOT overwrite an existing file unless `overwrite` is explicit (CLI: --force);
//  - without a network (QNetworkInformation: Disconnected) it does not even start: Failure::Offline.
//
// Test seam (without a network): the spec's downloadUrl may be a file:// URL (QNetworkAccessManager
// reads a local file), the NAM can be injected, and reachability can be overridden (setReachableOverride).
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

namespace tanara::net { class HttpClient; }

namespace tanara::processing {

class VoiceModelDownloader : public QObject {
    Q_OBJECT
public:
    // Why it failed (the UI writes human text from this; `error` is the technical detail).
    enum class Failure {
        None,
        Offline,        // no network (before or during the start: the machine cannot reach the server)
        Network,        // other network / HTTP error
        SizeMismatch,   // the downloaded size differs from the expected one; nothing was written
        ShaMismatch,    // the SHA-256 differs from the pinned one; the file waits as <target>.mismatch
        Write,          // the target file is not writable
        Exists,         // the target file already exists (without overwrite)
        Cancelled,      // cancel()
        NoUrl,          // the descriptor has no download address
    };
    Q_ENUM(Failure)

    // Check of an existing model file (the Settings > Speakers "verifying" state): the size, then the
    // pinned SHA-256.
    enum class VerifyResult {
        Ok,             // the size and the pinned SHA-256 match (or the size matches and no hash is pinned)
        Missing,        // no such file
        SizeMismatch,   // the size differs (corrupt / interrupted / another file)
        Unverifiable,   // the descriptor pins neither size nor hash, nothing to measure against (usable)
        ShaMismatch,    // the hash differs from the pinned one and the user has not accepted this file
        Unverified,     // the hash differs, but the user accepted exactly this file ("use it anyway")
    };
    Q_ENUM(VerifyResult)

    explicit VoiceModelDownloader(QObject* parent = nullptr);
    ~VoiceModelDownloader() override;

    // The HTTP client in use (not owned; nullptr = its own instance). Test seam / the shared client.
    void setHttpClient(net::HttpClient* http);

    // The download target: <modelsDir>/<fileName> (the first resolution place of VoiceModelRegistry).
    static QString targetPath(const VoiceModelSpec& spec, const QString& modelsDir);
    // Where a SHA-mismatching download waits for the user's decision: <target>.mismatch.
    static QString quarantinePath(const VoiceModelSpec& spec, const QString& modelsDir);
    static bool hasQuarantined(const VoiceModelSpec& spec, const QString& modelsDir);
    // Whether the user's override record (<target>.accepted-sha256) exists for the model file.
    static bool hasAcceptedRecord(const VoiceModelSpec& spec, const QString& modelsDir);
    // Lowercase hex SHA-256 of a file (empty if unreadable).
    static QString sha256OfFile(const QString& path);

    // Starts a download into the `target` file. true: it started (finished comes at the end). false: it
    // did not start (NoUrl / Exists / Write / Offline); then the finished(id, false, ...) signal is emitted
    // during the call, and lastFailure(id) tells the reason. If this model is already downloading: false,
    // without a signal (the running download goes on).
    bool start(const VoiceModelSpec& spec, const QString& target, bool overwrite = false);
    // Cancels a running download (the target file is not created). finished(id, false, ...) comes,
    // lastFailure = Cancelled. Does nothing for an id that is not running.
    void cancel(const QString& id);

    bool isRunning(const QString& id) const;
    QStringList runningIds() const;
    // Result of the id's last finished download (None: it succeeded or there has been none).
    Failure lastFailure(const QString& id) const;

    // Whether there is a network (QNetworkInformation; true if the platform has no backend). Test seam:
    // setReachableOverride(false/true), std::nullopt = the real state.
    bool networkReachable() const;
    void setReachableOverride(std::optional<bool> reachable);

    // Of the network errors, those of the "no internet" kind (name resolution, network down).
    static bool isOfflineError(int networkReplyError);

    // ---- verification and deletion ----
    // Synchronous check (CLI, tests). detail: a technical line (English), may be empty.
    // checkSha = false: only the (cheap) size check; the hash of a 70 MB file is for the async verify().
    static VerifyResult verifyFile(const VoiceModelSpec& spec, const QString& path, QString* detail = nullptr,
                                   bool checkSha = true);
    // Asynchronous check for the UI: verifyStarted(id) within the call, verified(id, ...) in the next
    // event-loop turn (until then isVerifying(id) is true: the "verifying" state).
    // false: already verifying or currently downloading (no signal).
    bool verify(const VoiceModelSpec& spec, const QString& path);
    bool isVerifying(const QString& id) const;
    // Result of the id's last finished verification (nullopt: there has been none).
    std::optional<VerifyResult> lastVerify(const QString& id) const;
    // Deletes the DOWNLOADED model file: only <modelsDir>/<fileName> (targetPath); does not touch the
    // one bundled in the package (<appDir>/models). false (+ *error, English): no downloaded file / not
    // deletable. The caller excludes a running download / verification (isRunning / isVerifying).
    // Also removes a quarantined file and the recorded override; the companions (LICENSE) go when no other
    // model file is left in the folder.
    static bool deleteModelFile(const VoiceModelSpec& spec, const QString& modelsDir, QString* error = nullptr);
    // The override: "use it anyway". Moves a quarantined (SHA-mismatching) file into place and records the
    // accepted hash of every mismatching file of the model (also one that already sits at the target, e.g.
    // copied by hand). false (+ *error, English): nothing to accept.
    static bool acceptMismatch(const VoiceModelSpec& spec, const QString& modelsDir, QString* error = nullptr);

signals:
    void started(const QString& id);
    // total: the size reported by the server, failing that the descriptor's sizeBytes (0 = unknown).
    void progress(const QString& id, qint64 received, qint64 total);
    void finished(const QString& id, bool ok, const QString& error);
    // Verification.
    void verifyStarted(const QString& id);
    void verified(const QString& id, tanara::processing::VoiceModelDownloader::VerifyResult result, const QString& detail);

private:
    struct Job;
    bool beginFile(const QString& id);
    void finishJob(const QString& id);
    void fail(const QString& id, Failure why, const QString& error);
    net::HttpClient* http();

    QPointer<net::HttpClient> m_http;
    net::HttpClient* m_ownHttp = nullptr;
    QHash<QString, Job*> m_jobs;
    QHash<QString, Failure> m_last;
    std::optional<bool> m_reachableOverride;
    QHash<QString, VerifyResult> m_verified;   // last verification per id
    QStringList m_verifying;                   // ids being verified now
};

} // namespace tanara::processing