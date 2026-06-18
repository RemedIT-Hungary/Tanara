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

    // A használatban lévő settings.json abszolút útja.
    QString settingsFilePath() const;

    // Sensible default beállítások (a contractban rögzítve).
    static AppSettings defaults(const QString& metadataDir = QString());

signals:
    void settingsChanged();

private:
    void ensureDirs() const;

    QString     m_metadataDir;
    AppSettings m_settings;
};

} // namespace tanara
