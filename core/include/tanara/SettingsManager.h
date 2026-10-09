#pragma once
//
// Tanara — AppSettings betöltése/mentése a <metadataDir>/settings.json-ból.
// Alapértelmezett metadataDir: ~/.tanara
//
#include "tanara/Types.h"
#include <QObject>

namespace tanara {

class SettingsManager : public QObject {
    Q_OBJECT
public:
    // metadataDir üres → ~/.tanara. A konstruktor betölti a beállításokat
    // (vagy a sensible defaultokat, ha nincs még settings.json).
    explicit SettingsManager(const QString& metadataDir = QString(),
                             QObject* parent = nullptr);

    const AppSettings& settings() const { return m_settings; }

    // Beállítja és perzisztálja, majd settingsChanged()-et emittál.
    void setSettings(const AppSettings& s);

    // Bekapcsolt beszélő-embedding modellek ("voiceModels"; ábécérendben, egyedi id-k).
    QStringList enabledVoiceModels() const { return m_settings.voiceModels; }
    // Normalizálja (ábécérend, egyedi), és ha változott: perzisztál + settingsChanged().
    void setEnabledVoiceModels(const QStringList& ids);

    // Betölt a settings.json-ból. Ha NINCS fájl → defaultok + mentés (első indítás). Ha VAN,
    // de nem olvasható / nem értelmezhető → defaultok CSAK a memóriában: a fájlt nem írjuk
    // felül csendben (a felhasználó beállításai és a másik folyamat épp írt fájlja ne vesszen).
    void load();

    // Lemezre ír, atomikusan (és létrehozza a hiányzó mappákat). Ha a betöltéskor a fájl
    // olvashatatlan volt, előbb félreteszi „settings.json.corrupt-<időbélyeg>” néven.
    void save() const;

    // A settings.json a betöltéskor létezett, de nem volt olvasható/értelmezhető (a futó
    // értékek defaultok; az eredeti fájl a lemezen érintetlen).
    bool loadFailed() const { return m_loadFailed; }

    // Első indítás: a settings.json ennél a betöltésnél még nem létezett (K-01 módválasztás).
    bool isFirstRun() const { return m_firstRun; }

    // A használatban lévő settings.json abszolút útja.
    QString settingsFilePath() const;

    // Sensible default beállítások (a contractban rögzítve).
    static AppSettings defaults(const QString& metadataDir = QString());

signals:
    void settingsChanged();

private:
    void ensureDirs() const;

    // A provider-réteg (STT/LLM) safety-net merge a `def` defaultokkal: üres provider-lista
    // vagy hiányzó/érvénytelen kiválasztott id esetén visszaesés a defaultra. A tényleges
    // betöltést+migrációt (új "sttProviders" / régi "stt" shape) már az appSettingsFromJson
    // végezte el a `loaded`-ben — itt csak a hiányokat pótoljuk.
    static void applyProviderDefaults(AppSettings& loaded, const AppSettings& def);

    QString     m_metadataDir;
    AppSettings m_settings;
    bool        m_firstRun = false;
    mutable bool m_loadFailed = false;   // olvashatatlan fájl a lemezen (mentéskor félretesszük)
};

} // namespace tanara
