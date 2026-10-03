#pragma once
//
// JobStats — korábbi futások mért sebessége, a hátralévő idő ŐSZINTE becsléséhez.
//
// A szolgáltatók (Soniox async, OpenAI-kompatibilis /audio/transcriptions) az átírás alatt
// NEM adnak százalékot — csak állapotot. Becslést ezért kizárólag a saját, korábbi sikeres
// futásainkból adunk: kulcsonként (pl. "transcribe/soniox") a „feldolgozási idő / hanghossz”
// arány mozgóátlaga. Minta nélkül nincs becslés (-1) — nem találunk ki számot.
//
// Tárolás: <metaadat-mappa>/job-stats.json (kicsi, bármikor törölhető).
//
#include <QString>
#include <QHash>

namespace tanara {

class JobStats {
public:
    // filePath üres → csak memóriában (teszt).
    explicit JobStats(const QString& filePath = QString());

    // Egy sikeres futás: wallSec mp alatt audioSec mp hangot dolgozott fel.
    void addSample(const QString& key, double wallSec, double audioSec);

    // Becsült teljes futásidő (mp) audioSec hosszú hangra; -1, ha a kulcshoz nincs minta.
    int estimateSec(const QString& key, double audioSec) const;

    int sampleCount(const QString& key) const;

private:
    struct Entry { double ratio = 0.0; int samples = 0; };
    void load();
    void save() const;

    QString m_filePath;
    QHash<QString, Entry> m_entries;
};

} // namespace tanara
