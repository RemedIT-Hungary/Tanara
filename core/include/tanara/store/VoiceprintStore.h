#pragma once
//
// VoiceprintStore — globális hang-lenyomat adatbázis (~/.tanara/voiceprints.json).
// Névhez több embedding tartozhat (más mikrofon/feltétel). A párosítás a személy
// lenyomat-halmazán a LEGNAGYOBB cosine-hasonlóságot veszi. Minden lokális (privacy).
//
// Több folyamat is írhatja: minden módosítás zár alatt visszaolvassa a lemez friss állapotát
// (ha változott), arra alkalmazza a változást, és atomikusan ír (lásd store/SharedFile.h).
//
#include "tanara/Types.h"
#include "tanara/store/SharedFile.h"
#include "tanara/voiceid/EmbeddingSet.h"

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

    // Lenyomat hozzáadása egy névhez. Üres print.id esetén generál egyet; üres print.model →
    // az alapmodell. print.dim-et az embedding méretére igazítja. Persist.
    void addPrint(const QString& name, Voiceprint print);
    // Lenyomat törlése id alapján (bármely személytől). true, ha törölt.
    bool removePrint(const QString& printId);
    // Személy átnevezése (a lenyomatok átkerülnek); ha a cél létezik, egyesít.
    void renamePerson(const QString& oldName, const QString& newName);
    // Személy összes lenyomatának törlése.
    void removePerson(const QString& name);
    // Két személy egyesítése: 'from' lenyomatai 'into'-ba, 'from' törlése.
    void merge(const QString& from, const QString& into);

    // Több modelles párosítás: személyenként modellenként a MAX cosine a személy adott modellű
    // lenyomatain, majd ezek átlaga a közös modelleken (a kért modellIds közül azokon, amelyekhez
    // a lekérdezésben van vektor ÉS a személynek van lenyomata). Nincs közös modell → -1.
    // Legjobb párosítás; üres DB / üres lekérdezés / sehol közös modell → { "", -1 }.
    VoiceMatch bestMatch(const EmbeddingSet& query, const QStringList& modelIds) const;
    // Minden személy pontszáma csökkenő sorrendben (UI/diagnosztika); közös modell nélkül -1.
    QVector<VoiceMatch> rankedMatches(const EmbeddingSet& query, const QStringList& modelIds) const;
    // Egymodelles (régi) forma: {"campplus" → embedding}.
    VoiceMatch bestMatch(const QVector<float>& embedding) const;
    QVector<VoiceMatch> rankedMatches(const QVector<float>& embedding) const;

    // A személy azon mintái (sampleRef, ábécérendben, egyedi), amelyekhez még nincs az adott
    // modellel készült lenyomat (a lusta pótláshoz). Üres sampleRef-ű lenyomat nem számít.
    QStringList printsMissingModel(const QString& name, const QString& modelId) const;

    // Egy lenyomat és a gazdája id alapján; false, ha nincs ilyen.
    bool findPrint(const QString& printId, QString* owner, Voiceprint* print) const;
    // Ha a fájl a lemezen megváltozott (másik folyamat írta), újraolvassa.
    void refresh() { reloadIfChanged(); }

    QString filePath() const { return m_filePath; }

    // --- segéd-matek (publikus + statikus, hogy tesztelhető és újrahasznosítható) ---
    static double cosineSimilarity(const QVector<float>& a, const QVector<float>& b);
    static QVector<float> l2normalize(const QVector<float>& v);

private:
    void load();
    void reloadIfChanged();   // ha a fájl a lemezen megváltozott (másik folyamat írta)
    void persist();

    QString m_filePath;
    QMap<QString, QVector<Voiceprint>> m_people;   // név → lenyomatok
    FileStamp m_stamp;            // a fájl állapota az utolsó betöltéskor / mentéskor
    bool      m_corrupt = false;  // a lemezen lévő fájl értelmezhetetlen (mentéskor félretesszük)
};

} // namespace tanara
