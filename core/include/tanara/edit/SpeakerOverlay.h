#pragma once
//
// SpeakerOverlay — a meeting kézi beszélő-javításainak PERZISZTENS rétege
// (<meeting-mappa>/transcript.speakers.json). A nyers diarizáció (transcript.tokens.json /
// transcript.segments.json) érintetlen marad; a javítások erre rétegződnek, ezért
// visszavonhatók és túlélik a résztvevő-azonosítás újrafuttatását. Újra-átíráskor
// eldobjuk (a megszólalások határai megváltoznak).
//
// A megjelenített név feloldási sorrendje:
//   soronkénti felülírás → (összevont nyers címke átirányítása) → Meeting.speakerMap → nyers címke.
// A Meeting.speakerMap szemantikája változatlan (régi Widgets UI + CLI ezt olvassa/írja).
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// A transcript.segments.json egy sora + a hozzá képzett stabil azonosító.
struct TranscriptLine {
    QString id;         // "u<startMs>" (ütközésnél "-2", "-3" … utótag a fájl sorrendjében)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString rawLabel;   // nyers diarizációs címke
    QString text;
};

// Kézzel felvett résztvevő (akit a diarizáció nem különített el).
struct OverlayParticipant {
    QString key;        // "participant:N"
    QString person;     // kötött személy neve; üres = névtelen
    QString label;      // névtelen címke („Új beszélő 1")
    int     colorIndex = 0;
};

// Egy megszólalás felülírása / jelzői.
struct OverlayUtterance {
    QString speaker;            // felülírt beszélő-kulcs; üres = nincs felülírás
    bool    corrected = false;  // kézzel átsorolt
    bool    confirmed = false;  // „Jó így"
    bool isDefault() const { return speaker.isEmpty() && !corrected && !confirmed; }
};

struct OverlayIdentification {
    QString person;
    double  score = -1.0;
};

struct SpeakerOverlay {
    QString transcriptFingerprint;                  // melyik átirathoz tartozik (lásd lent)
    QVector<OverlayParticipant> participants;
    int nextParticipant = 1;                        // a következő "participant:N" sorszáma
    int nextAnonymous = 1;                          // a következő „Új beszélő N" sorszáma
    QMap<QString, QString> merged;                  // összevont nyers címke → cél beszélő-kulcs
    QStringList removedRaw;                         // eltávolított (üres) nyers beszélő-oszlopok
    QMap<QString, OverlayUtterance> utterances;     // megszólalás-id → felülírás
    QMap<QString, OverlayIdentification> identified;// nyers címke → hang-azonosítás eredménye
    QStringList changedSinceSummary;                // az összefoglaló óta javított beszélők kulcsai
    int summaryEpoch = 0;                           // nő, ha az összefoglaló újragenerálódik / elfogadják

    // Van-e sor-/beszélő-szintű javítás (az azonosítás és az elavult-jelző nem számít).
    bool hasEdits() const {
        return !participants.isEmpty() || !merged.isEmpty() || !removedRaw.isEmpty()
            || !utterances.isEmpty();
    }
    bool isEmpty() const {
        return !hasEdits() && identified.isEmpty() && changedSinceSummary.isEmpty()
            && summaryEpoch == 0;
    }
    const OverlayParticipant* participant(const QString& key) const;
    OverlayParticipant* participant(const QString& key);
};

namespace speakeredit {

// ---- fájlok ---------------------------------------------------------------
QString overlayPath(const QString& meetingFolder);      // transcript.speakers.json
QString segmentsPath(const QString& meetingFolder);     // transcript.segments.json
QString mixdownPath(const Meeting& m);                  // a voice-ID hangforrása (lehet, hogy nem létezik)

// A transcript.segments.json sorai stabil azonosítóval. Üres, ha nincs átirat.
QVector<TranscriptLine> loadTranscriptLines(const QString& meetingFolder);
// Az átirat ujjlenyomata (sorok kezdete/vége/címkéje) — ebből derül ki, ha az overlay
// egy korábbi átirathoz tartozik.
QString transcriptFingerprint(const QVector<TranscriptLine>& lines);

// Az overlay nyersen, ahogy a lemezen van (hiányzó fájl → üres overlay; a régi meetingek
// így változatlanul betöltődnek).
SpeakerOverlay loadOverlay(const QString& meetingFolder);
// Ugyanez, de az aktuális átirathoz VALIDÁLVA: ha az ujjlenyomat nem egyezik, a
// sor-/beszélő-szintű javítások eldobódnak (az azonosítás + elavult-jelző megmarad).
SpeakerOverlay loadOverlayFor(const QString& meetingFolder, const QVector<TranscriptLine>& lines);
// Mentés (QSaveFile). Teljesen üres overlay → a fájl törlődik.
bool saveOverlay(const QString& meetingFolder, const SpeakerOverlay& overlay);

// ---- feloldás -------------------------------------------------------------
bool isParticipantKey(const QString& key);
// Egy sor feloldott beszélő-kulcsa: felülírás → összevonás-átirányítás → nyers címke.
QString resolveSpeakerKey(const SpeakerOverlay& ov, const TranscriptLine& line);
// A beszélő személye (üres = névtelen) és megjelenített neve.
QString speakerPerson(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                      const QString& key);
QString speakerDisplayName(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                           const QString& key);

// A tokenek beszélőjét a FELOLDOTT névre írja (overlay + speakerMap). Az
// AppController ezt hívja mindenhol, ahol a neveket az átiratra kell tenni
// (transcript.md, összefoglaló-bemenet). Overlay nélkül = a régi speakerMap-alkalmazás.
void applyResolvedSpeakers(MergedTranscript& mt, const Meeting& m);
// A transcript.md újragenerálása a feloldott nevekkel (a transcript.tokens.json-ból).
bool regenerateTranscriptMarkdown(const Meeting& m);

// ---- összefoglaló-elavultság ---------------------------------------------
SummaryStaleInfo summaryStale(const Meeting& m);
// Beszélő-változás az overlay-en KÍVÜLRŐL (régi UI átnevezés, auto-azonosítás): ha van
// összefoglaló, elavultnak jelöli. true, ha változott az állapot.
bool markSummaryStale(const Meeting& m, const QStringList& speakerKeys);
// Ugyanez, de megmondja, mely kulcsok kerültek ÚJONNAN a listára (a visszavonáshoz).
QStringList markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys);
// Egy markSummaryStaleKeys visszavonása: a megadott kulcsok lekerülnek az elavult-listáról.
// true, ha változott az állapot.
bool unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys);
// Összefoglaló (újra)generálva VAGY a felhasználó elfogadta („Rendben így").
bool clearSummaryStale(const QString& meetingFolder);

// ---- az AppController horgai ---------------------------------------------
// A hang-azonosítás pontszámának rögzítése (nyers címke → személy, cosine).
void recordIdentification(const QString& meetingFolder, const QString& rawLabel,
                          const QString& person, double score);
// Globális személy-átnevezés / -törlés átvezetése a kézzel felvett résztvevőkön.
bool renamePersonInOverlay(const QString& meetingFolder, const QString& oldName,
                           const QString& newName);
bool removePersonFromOverlay(const QString& meetingFolder, const QString& name);

// ---- újra-átírás ----------------------------------------------------------
RetranscribeImpact retranscribeImpact(const Meeting& m);
// A jelenlegi átirat-fájlok (md, tokens, segments, speakers + a speakerMap) másolata a
// meeting-mappa "transcript-backup-<időbélyeg>" almappájába. Vissza: a mappa útja (üres = hiba).
QString backupTranscript(const Meeting& m);
// Új átirat készült: az overlay sor-szintű javításai + a megszólalás-embedding cache törlése.
void discardForNewTranscript(const QString& meetingFolder);

} // namespace speakeredit
} // namespace tanara
