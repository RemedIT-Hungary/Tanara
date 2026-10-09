#pragma once
//
// VoiceModelRegistry — a beépített beszélő-embedding modellek leírói (id, fájlnév, fbank-
// paraméterek, licenc, forrás) és a modellfájl feloldása. Egyetlen feloldási szabály:
// <metaDir>/models/<fileName>, különben <appDir>/models/<fileName>.
// Az engedélyezett modellek listája a settings.json "voiceModels" kulcsa (SettingsManager).
//
#include "tanara/voiceid/VoiceEmbedder.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara {

struct VoiceModelSpec {
    QString id;               // stabil azonosító, pl. "campplus" (kisbetű, kötőjel)
    QString displayName;      // "CAM++ (3D-Speaker)"
    QString fileName;         // ONNX fájlnév a models/ mappában
    EmbedderConfig features;  // fbank paraméterek ehhez a modellhez
    int     dim = 0;          // várt dimenzió (0 = ismeretlen az első inferenciáig)
    QString license;          // "Apache-2.0" stb.
    QString sourceUrl;        // a modell (és licence) leírólapja
    QString downloadUrl;      // közvetlen ONNX-letöltés (sherpa-onnx release asset)
    qint64  sizeBytes = 0;    // a letöltött fájl várt mérete (0 = ismeretlen)
};

class VoiceModelRegistry {
public:
    // Az alapmodell id-ja (a régi, modell-mező nélküli lenyomatok és cache-ek ehhez tartoznak).
    static QString defaultModelId() { return QStringLiteral("campplus"); }

    // Beépített leírók, id szerint ábécérendben: "campplus", "eres2netv2", "wespeaker-resnet34-lm".
    static QVector<VoiceModelSpec> builtin();
    static std::optional<VoiceModelSpec> spec(const QString& id);

    // <metaDir>/models/<fileName>, ha létezik; különben <appDir>/models/<fileName>, ha létezik;
    // különben az első (a „várt hely”, hibaüzenethez / letöltési célnak). appDir üres is lehet.
    static QString resolvePath(const VoiceModelSpec& spec, const QString& metaDir, const QString& appDir);
    // A beépített modellek közül azok, amelyeknek a fájlja megvan.
    static QVector<VoiceModelSpec> available(const QString& metaDir, const QString& appDir);

    // A használható modellek: engedélyezett ∩ elérhető (fájl megvan), id szerint ábécérendben.
    // Az ismeretlen / hiányzó fájlú engedélyezett id-k kimaradnak.
    static QVector<VoiceModelSpec> active(const QStringList& enabledIds, const QString& metaDir,
                                          const QString& appDir);

    // Id-lista determinisztikus alakja: trimmelt, üresek nélkül, egyedi, ábécérendben.
    static QStringList normalizeIds(const QStringList& ids);
};

} // namespace tanara
