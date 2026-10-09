#pragma once
//
// Megszólalásonkénti beszélő-embedding: az absztrakt embedder (hogy a unit-tesztek hamisat
// adhassanak be), a valódi (VoiceEmbedder / ONNX) megvalósítás gyára, és a meeting-mappában élő,
// modellenkénti cache (transcript.embeddings.bin, v2).
//
// A valódi megvalósítás a hangot EGYSZER dekódolja 16 kHz mono-ra (egy ffmpeg-futás), és
// abból szeletel — megszólalásonkénti ffmpeg 400–1500 sornál túl lassú lenne. Több modell
// esetén is egy dekódolás: minden modell ugyanabból a PCM-ből számol.
//
#include "tanara/voiceid/EmbeddingSet.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/voiceid/VoiceModelRegistry.h"
#include "tanara/voiceid/VoiceEmbedderSet.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class IUtteranceEmbedder {
public:
    virtual ~IUtteranceEmbedder() = default;
    // A meeting hangjának megnyitása (egyszeri dekódolás). false = nincs modell / hang.
    virtual bool open(const QString& audioPath) = 0;
    // A [startMs,endMs] szelet L2-normalizált embeddingje az alapmodellel
    // (VoiceModelRegistry::defaultModelId()). Üres vektor = nem sikerült.
    virtual QVector<float> embed(qint64 startMs, qint64 endMs) = 0;
    // A szelet embeddingje a kért modellekkel. Az eredményben minden olyan kért modell kulcsa
    // szerepel, amelyet ez az embedder ismer (üres vektor = nem sikerült, ne próbáljuk újra);
    // a hiányzó kulcs = ez a modell itt nem elérhető. Alapértelmezés: csak az alapmodell,
    // az embed()-del (az egymodelles hamis embedderek így változatlanul működnek).
    virtual EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds);
    virtual QString lastError() const { return QString(); }
};

// Gyár: a háttérszál a SAJÁT példányát hozza létre vele (az embedder nem szálak közt megosztott).
using UtteranceEmbedderFactory = std::function<std::unique_ptr<IUtteranceEmbedder>()>;

// Egy modell a valódi embedderhez: id + ONNX-fájl + fbank-paraméterek.
struct UtteranceModel {
    QString id;
    QString path;
    EmbedderConfig features;
    int dim = 0;   // várt dimenzió (0 = nem ellenőrizzük; eltérésnél a VoiceEmbedder figyelmeztet)
};

// A valódi megvalósítás gyára több modellre: EGY dekódolt PCM, modellenként egy VoiceEmbedder.
// Az open() akkor sikeres, ha a hang dekódolható és legalább egy modell betölthető; a be nem
// tölthető modellek kulcsa hiányzik az embedAll() eredményéből. Voice-ID nélküli buildben
// (TANARA_BUILD_VOICEID=OFF) az open() false.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QVector<UtteranceModel>& models, const QString& ffmpegPath = QStringLiteral("ffmpeg"));
// Ugyanez egy modell-készletből (a betöltője is: tesztben hamis modell). Üres készlet → üres gyár.
// Minden létrehozott embedder a készlet saját, betöltetlen másolatát kapja.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const VoiceEmbedderSet& set, const QString& ffmpegPath = QStringLiteral("ffmpeg"));
// A megadott modell-leírók (pl. VoiceModelRegistry::active()) a feloldott fájl-útjukkal.
QVector<UtteranceModel> utteranceModelsFor(const QVector<VoiceModelSpec>& specs,
                                           const QString& metaDir, const QString& appDir);
// Egymodelles rövidítés: az alapmodell (VoiceModelRegistry::defaultModelId()) a megadott fájlból.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QString& modelPath, const QString& ffmpegPath = QStringLiteral("ffmpeg"));

// Modell → (megszólalás-id → embedding), a meeting-mappában perzisztálva (v2). Az átirat
// ujjlenyomatához kötött: más átirat → üres cache. A v1 (egymodelles) fájl betöltéskor
// eldobódik. Üres vektor = megpróbáltuk, nem sikerült (ne próbáljuk újra).
struct UtteranceEmbeddingCache {
    QString fingerprint;
    QMap<QString, QHash<QString, QVector<float>>> models;   // modelId → utteranceId → vektor

    // Egy modell összes vektora (üres hash, ha nincs).
    QHash<QString, QVector<float>> vectors(const QString& modelId) const { return models.value(modelId); }
    void set(const QString& modelId, const QString& utteranceId, const QVector<float>& vec)
    {
        models[modelId].insert(utteranceId, vec);
    }
    // Megpróbáltuk-e már (akár sikertelenül) ezt a sort ezzel a modellel.
    bool has(const QString& modelId, const QString& utteranceId) const;
    // A sor vektorai a kért modellekkel (csak a nem üresek).
    EmbeddingSet setFor(const QString& utteranceId, const QStringList& modelIds) const;
    // A felsorolt sorok közül azok, amelyeket ezzel a modellel még nem próbáltunk (sorrendtartó).
    QStringList missingFor(const QString& modelId, const QStringList& utteranceIds) const;
    // Nincs egyetlen bejegyzés sem (egy modellel sem).
    bool isEmpty() const;

    static QString filePath(const QString& meetingFolder);   // transcript.embeddings.bin
    static UtteranceEmbeddingCache load(const QString& meetingFolder, const QString& fingerprint);
    bool save(const QString& meetingFolder) const;
    static void remove(const QString& meetingFolder);
};

} // namespace tanara
