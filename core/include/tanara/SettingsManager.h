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

    // Betölt a settings.json-ból (ha nincs/hibás → defaultok + mentés).
    void load();

    // Lemezre ír (és létrehozza a hiányzó mappákat).
    void save() const;

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
};

} // namespace tanara
