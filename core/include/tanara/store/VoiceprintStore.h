#pragma once
//
// VoiceprintStore — globális hang-lenyomat adatbázis (~/.tanara/voiceprints.json).
// Névhez több embedding tartozhat (más mikrofon/feltétel). A párosítás a személy
// lenyomat-halmazán a LEGNAGYOBB cosine-hasonlóságot veszi. Minden lokális (privacy).
//
#include "tanara/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace tanara {

class VoiceprintStore {
public:
    explicit VoiceprintStore(const QString& filePath = QString()); // üres → ~/.tanara/voiceprints.json

    // Azon nevek, amelyekhez van legalább egy lenyomat (ábécé-rendben).
    QStringList people() const;
    QVector<Voiceprint> printsFor(const QString& name) const;
    int printCount(const QString& name) const;
    int totalPrintCount() const;

    // Lenyomat hozzáadása egy névhez. Üres print.id esetén generál egyet.
    // print.dim-et az embedding méretére igazítja. Persist.
    void addPrint(const QString& name, Voiceprint print);
    // Lenyomat törlése id alapján (bármely személytől). true, ha törölt.
    bool removePrint(const QString& printId);
    // Személy átnevezése (a lenyomatok átkerülnek); ha a cél létezik, egyesít.
    void renamePerson(const QString& oldName, const QString& newName);
    // Személy összes lenyomatának törlése.
    void removePerson(const QString& name);
    // Két személy egyesítése: 'from' lenyomatai 'into'-ba, 'from' törlése.
    void merge(const QString& from, const QString& into);

    // Legjobb párosítás: a legnagyobb cosine-t adó személy (max a halmazán).
    // Üres DB / üres embedding → { "", -1 }.
    VoiceMatch bestMatch(const QVector<float>& embedding) const;
    // Minden személy pontszáma csökkenő sorrendben (UI/diagnosztika).
    QVector<VoiceMatch> rankedMatches(const QVector<float>& embedding) const;

    QString filePath() const { return m_filePath; }

    // --- segéd-matek (publikus + statikus, hogy tesztelhető és újrahasznosítható) ---
    static double cosineSimilarity(const QVector<float>& a, const QVector<float>& b);
    static QVector<float> l2normalize(const QVector<float>& v);

private:
    void load();
    void persist() const;

    QString m_filePath;
    QMap<QString, QVector<Voiceprint>> m_people;   // név → lenyomatok
};

} // namespace tanara
