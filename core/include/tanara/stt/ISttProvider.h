#pragma once
//
// STT plugin-szerződés. Async, signal-alapú (illeszkedik a Qt event loophoz),
// és előre felkészítve a későbbi élő/streaming módra (startLive()).
//
#include "tanara/Types.h"
#include <QObject>

namespace tanara {

// Egy futó átírási feladat. A hívó a finished/failed jelekre köt.
class SttJob : public QObject {
    Q_OBJECT
public:
    explicit SttJob(QObject* parent = nullptr) : QObject(parent) {}
    ~SttJob() override = default;
    virtual void cancel() = 0;

signals:
    void stateChanged(tanara::JobState state);
    void progress(int percent, const QString& message);
    void finished(const tanara::TrackTranscript& result);
    void failed(const QString& error);
};

struct SttRequest {
    QString audioFilePath;          // egy sáv .ogg fájlja
    QString trackId;
    QString speakerLabel;           // a sávhoz tartozó fix beszélő
    QStringList languageHints{QStringLiteral("hu")};
    bool diarization = false;       // per-sávnál általában false (a sáv = a beszélő)
    // Context-envelope: szabad szöveg, amivel a STT jobban dönt a kétes részeknél
    // (meeting-cím + résztvevők + a felhasználó pár szavas téma-leírása; később
    // naptár-bejegyzés). A Soniox a „context" mezőként küldi; a context-et nem támogató
    // providerek figyelmen kívül hagyják.
    QString context;
    QVariantMap providerOptions;
};

class ISttProvider {
public:
    virtual ~ISttProvider() = default;
    virtual QString name() const = 0;
    virtual bool supportsLive() const = 0;

    // A visszaadott job a hívóé (parentelni / finish után deleteLater).
    virtual SttJob* transcribe(const SttRequest& req) = 0;

    // Jövőbeli élő mód — alapból nincs, a provider opt-inol később.
    virtual SttJob* startLive(const SttRequest& /*req*/) { return nullptr; }
};

} // namespace tanara
