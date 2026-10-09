#pragma once
//
// VoiceEmbedderSet — a használt (engedélyezett ∩ elérhető) beszélő-modellek készlete: modellenként
// egy lustán (első használatkor) betöltött VoiceEmbedder. Egy PCM-szeletből minden modell
// beágyazását adja (EmbeddingSet). Nem szálbiztos: háttérszál a freshCopy()-val saját példányt kap.
//
// Teszt-varrat: a betöltő (PcmEmbedderLoader) cserélhető — így hamis „modell” is beadható, ONNX
// nélkül. Alapból a valódi VoiceEmbedder tölt (voice-ID nélküli buildben semmi sem töltődik).
//
#include "tanara/voiceid/EmbeddingSet.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

// Egy betöltött modell: 16 kHz mono PCM ([-1,1]) → L2-normalizált vektor (üres = nem sikerült).
using PcmEmbedder = std::function<QVector<float>(const QVector<float>& mono16k)>;
// Modell betöltése a feloldott fájlból; üres függvény = nem tölthető be (error: az ok).
using PcmEmbedderLoader =
    std::function<PcmEmbedder(const VoiceModelSpec& spec, const QString& path, QString* error)>;

// Egy használt modell: a leíró + a feloldott fájl.
struct VoiceModelEntry {
    VoiceModelSpec spec;
    QString path;
};

class VoiceEmbedderSet {
public:
    // models: a használt modellek (bármilyen sorrendben; belül id szerint rendez). loader üres →
    // a valódi VoiceEmbedder (defaultLoader()).
    explicit VoiceEmbedderSet(QVector<VoiceModelEntry> models = {}, PcmEmbedderLoader loader = {});
    // A beállítás szerinti készlet: VoiceModelRegistry::active(enabledIds, metaDir, appDir).
    static VoiceEmbedderSet fromSettings(const QStringList& enabledIds, const QString& metaDir,
                                         const QString& appDir, PcmEmbedderLoader loader = {});
    static PcmEmbedderLoader defaultLoader();

    // Ugyanez a konfiguráció betöltetlenül (háttérszálnak; a modellek ott töltődnek be újra).
    VoiceEmbedderSet freshCopy() const;

    QStringList modelIds() const;                    // ábécérendben
    bool isEmpty() const { return m_models.isEmpty(); }
    const QVector<VoiceModelEntry>& models() const { return m_models; }
    PcmEmbedderLoader loader() const { return m_loader; }

    // Betölti a még nem próbált modelleket; true, ha legalább egy használható.
    bool ensureLoaded() const;
    // A betölthető modellek id-i (betöltést vált ki).
    QStringList loadedModelIds() const;

    // Minden betölthető modell vektora (a sikertelen / be nem tölthető modell kulcsa hiányzik).
    EmbeddingSet embedPcm(const QVector<float>& mono16k) const;
    // Csak a felsorolt modellekkel; a betölthető, kért modell kulcsa üres vektorral is szerepel
    // (= megpróbáltuk, nem sikerült), a be nem tölthetőé hiányzik.
    EmbeddingSet embedPcmWith(const QVector<float>& mono16k, const QStringList& modelIds) const;

    QString lastError() const { return m_error; }

private:
    struct Slot {
        bool tried = false;
        PcmEmbedder fn;
    };
    Slot& slot(int i) const;

    QVector<VoiceModelEntry> m_models;
    PcmEmbedderLoader m_loader;
    mutable QVector<Slot> m_slots;
    mutable QString m_error;
};

} // namespace tanara
