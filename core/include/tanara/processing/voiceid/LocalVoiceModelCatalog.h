#pragma once
//
// LocalVoiceModelCatalog: the local IModelCatalog (Origin::Local) of the speaker-embedding models: a
// wrapper around VoiceModelRegistry (the built-in descriptors, the file resolution) and
// VoiceModelDownloader (the download). list(VoiceEmbedding) gives the built-in models with their
// state; ensureAvailable(id) gives the local file, downloading it first if it is missing.
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/providers/catalog/IModelCatalog.h"

#include <QObject>
#include <QStringList>

namespace tanara::net { class HttpClient; }

namespace tanara::processing {

class LocalVoiceModelCatalog : public QObject, public providers::IModelCatalog {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("local-voice-models");

    // specs: the models offered (empty = VoiceModelRegistry::builtin()); a test passes file:// URLs.
    LocalVoiceModelCatalog(QString modelsDir, QString appDir, QVector<VoiceModelSpec> specs = {},
                           net::HttpClient* http = nullptr, QObject* parent = nullptr);

    QString name() const override { return Id; }
    providers::ModelListJob* list(providers::CapabilityKind capability) override;
    providers::ModelReadyJob* ensureAvailable(const QString& modelId, providers::ProgressSink progress = {}) override;

    // The downloader in use (test seam: reachability override).
    VoiceModelDownloader* downloader() const { return m_downloader; }

private:
    QString m_modelsDir, m_appDir;
    QVector<VoiceModelSpec> m_specs;
    VoiceModelDownloader* m_downloader = nullptr;
};

} // namespace tanara::processing
