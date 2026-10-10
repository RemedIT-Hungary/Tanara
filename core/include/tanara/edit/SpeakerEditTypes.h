#pragma once
//
// Tanara — az átirat-szerkesztő (beszélő-javítás) value-típusai: a backend ⟷ UI szerződés.
// Sima adat-struktúrák (QObject nélkül), queued signalban is átküldhetők.
//
// Fogalmak:
//  - MEGSZÓLALÁS (utterance): a transcript.segments.json egy sora. Soron belüli vágás nincs.
//  - BESZÉLŐ (speaker): a meeting egy résztvevője, stabil kulccsal. Vagy egy nyers
//    diarizációs címke („Beszélő 1" — a kulcs maga a címke), vagy kézzel felvett résztvevő
//    („participant:N"). Személyhez (névhez) köthető, vagy névtelen.
//
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>

namespace tanara {

// Egy megszólalás FELOLDOTT állapota (a UI lista-modelljének egy sora).
struct EditorUtterance {
    QString id;                 // stabil azonosító (amíg az átirat nem generálódik újra)
    int     index = -1;         // sorszám a transcript.segments.json-ban (időrend)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString text;
    QString speakerKey;         // a feloldott beszélő kulcsa (EditorSpeaker::key)
    QString rawLabel;           // a nyers diarizációs címke (érintetlen)
    bool    uncertain = false;          // hangra gyengén illik a beszélőjéhez (sraffozott)
    bool    manuallyCorrected = false;  // kézzel átsorolt („javítva")
    bool    confirmed = false;          // a felhasználó megerősítette („Jó így")
    // „Egymásra beszéltek": nem használjuk hangmintának (centroid, hanglenyomat). Automatikus
    // (átfedés más beszélővel) vagy kézi; a kézi felülírás mindkét irányba érvényes.
    bool    noisy = false;
    bool    noisyOverlap = false;       // az átfedés-szabály szerint zajos (a kézi felülírástól függetlenül)
    bool    rechecked = false;          // az újraellenőrzés jelölte bizonytalannak
    // A hangra jobban illő beszélő kulcsa (az újraellenőrzés javaslata); üres = nincs.
    QString likelySpeakerKey;
};

// Egy kötés a résztvevő-jóváhagyáshoz (SpeakerEditor::applyBindings): a nyers beszélő (vagy
// csak a megadott sorai) személyhez kerül; üres personName = névtelenre állítás.
struct SpeakerBinding {
    QString     rawLabel;
    QString     personName;
    QStringList utteranceIds;   // üres = a nyers beszélő egésze
};

// A meeting egy beszélője (a sáv/oszlop a szerkesztőben).
struct EditorSpeaker {
    QString key;                // stabil kulcs: nyers címke VAGY "participant:N"
    QString displayName;        // személynév, vagy névtelen címke („Beszélő 2", „Új beszélő 1")
    QString personName;         // a kötött személy neve; üres = névtelen
    QString rawLabel;           // nyers címke (kézzel felvett résztvevőnél üres)
    bool    anonymous = true;   // nincs személyhez kötve
    bool    added = false;      // kézzel felvett résztvevő (nem a diarizációból jön)
    bool    isSelf = false;     // a felhasználó saját maga (userSpeakerName)
    int     colorIndex = 0;     // első megjelenés sorrendje a meetingben; szerkesztéstől független
    int     utteranceCount = 0;
    qint64  talkTimeMs = 0;
    double  talkShare = 0.0;    // 0..1, a teljes beszédidő hányada
    bool    hasVoiceprint = false;      // a kötött személynek van hanglenyomata
    double  voiceConfidence = -1.0;     // hang-azonosítás cosine pontszáma; <0 = nem ismert
};

// Kézi átsorolás utáni javaslat: a forrás-beszélőnél maradt sorok, amelyek hangra a
// célhoz állnak közelebb („Még 14 sor hasonlít erre a hangra. Átrakjam?").
struct SpeakerSuggestion {
    QString     sourceSpeakerKey;
    QString     targetSpeaker;      // a cél-beszélő kulcsa
    QString     anchorUtteranceId;  // a kézzel átsorolt sor (a UI ez alá teszi a dobozt)
    QStringList utteranceIds;
    bool isValid() const { return !targetSpeaker.isEmpty() && !utteranceIds.isEmpty(); }
};

// Kézi átsorolás után, ha a „hasonló sorok" javaslat azért hallgat, mert a két hang túl
// hasonló („nem két ember" őr), de mindkét beszélő elnevezett, és mindkettőnek van legalább
// 3 megerősített / javított (embeddelt, tiszta) sora: a szerkesztő felajánlja a kettejük
// közötti átnézést („A és B hangja hasonló. Nézzem át kettejük sorait…?").
struct PairRecheckOffer {
    QString sourceSpeakerKey;       // ahonnan a sor(ok) jöttek
    QString targetSpeakerKey;       // ahova kerültek
    double  centroidSimilarity = 0.0;   // a „hasonló sorok" 2-közepének két hangja közti cosine
    bool isValid() const { return !sourceSpeakerKey.isEmpty() && !targetSpeakerKey.isEmpty(); }
};

// Egy ismert személy a személyválasztó panelhez.
struct PersonInfo {
    QString name;
    bool    hasVoiceprint = false;
    int     voiceprintCount = 0;
    int     meetingCount = 0;       // hány meetingen szerepel
    QStringList aliases;            // becenevek (people.json) — a keresés ezekre is talál
    QString matchedAlias;           // szűrés után: ha a találat becenévből jött, az a becenév
};

// Mennyi anyag van egy beszélő kézi hanglenyomatához ebben a meetingben.
struct VoiceprintMaterial {
    int    usableLines = 0;     // elég hosszú (és nem bizonytalan) sorok száma
    qint64 usableMs = 0;        // ezek felhasználható hossza összesen
    qint64 missingMs = 0;       // ennyi hiányzik a minimumhoz (0 = elég)
    bool   sufficient = false;
};

// A kézi hanglenyomat-készítés eredménye.
struct VoiceprintResult {
    bool    ok = false;
    QString printId;            // a létrejött lenyomat azonosítója
    QString error;              // emberi hibaüzenet, ha !ok
    int     usedLines = 0;
    qint64  usedMs = 0;
    qint64  missingMs = 0;      // ha nincs elég anyag: ennyi hiányzik
};

// Az összefoglaló elavultsága: a készítése óta változott a beszélő-hozzárendelés.
// Célzott elavulás (ha az összefoglalónak vannak forrás-hivatkozásos állításai): mely
// állítások / teendők forrás-sorainak beszélője változott. Állítások nélkül (régi vagy
// témánkénti összefoglaló) targeted == false, és csak az egész-dokumentum jelzés él.
struct SummaryStaleInfo {
    bool stale = false;
    int  correctedSpeakers = 0; // „Az összefoglaló óta N beszélőt javítottál"
    bool targeted = false;              // van statement-szintű adat (a lenti mezők érvényesek)
    int  affectedStatements = 0;        // érintett állítás + döntés („2 állítás")
    int  affectedTodos = 0;             // érintett teendő („1 teendő")
    int  ownerChanges = 0;              // ebből: teendő, amelynek a felelőse is érintett
    QStringList affectedStatementIds;   // a SummaryStatement::id-k
    QStringList affectedUtteranceIds;   // a térkép kiemeléséhez
};

// Mi vész el újra-átíráskor (a megerősítő párbeszédhez).
struct RetranscribeImpact {
    int correctedUtterances = 0;    // kézzel átsorolt sorok
    int confirmedUtterances = 0;    // „Jó így" sorok (amelyek nem átsoroltak)
    int namedSpeakers = 0;          // nevesített beszélők (speakerMap + nevesített résztvevők)
    int addedParticipants = 0;      // kézzel felvett résztvevők
    int manualCorrections() const { return correctedUtterances + confirmedUtterances; }
    bool any() const { return manualCorrections() > 0 || namedSpeakers > 0 || addedParticipants > 0; }
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::EditorUtterance)
Q_DECLARE_METATYPE(tanara::EditorSpeaker)
Q_DECLARE_METATYPE(tanara::SpeakerSuggestion)
Q_DECLARE_METATYPE(tanara::PersonInfo)
Q_DECLARE_METATYPE(tanara::VoiceprintMaterial)
Q_DECLARE_METATYPE(tanara::VoiceprintResult)
Q_DECLARE_METATYPE(tanara::SummaryStaleInfo)
Q_DECLARE_METATYPE(tanara::RetranscribeImpact)
