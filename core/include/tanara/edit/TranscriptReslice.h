#pragma once
//
// TranscriptReslice — egy MEGLÉVŐ átirat újraszeletelése a transcript.tokens.json-ból, a Soniox
// token-időzítés korrekciójával (tanara/stt/SonioxTiming.h), az utterance id-k átvezetésével.
//
// A megszólalás-id a kezdőidőből képződik („u<startMs>"), ezért az idő-korrekció minden id-t
// megváltoztat. A régi sor (transcript.segments.json) → új sor térkép: ugyanaz a nyers beszélő,
// és az új sor tartalmazza a régi kezdetét (a korrekció csak szűkíti a szüneteket: a sorok
// legfeljebb összeolvadhatnak, szét nem válnak). A térképet az overlay (transcript.speakers.json:
// minden id-hivatkozás + az átirat-ujjlenyomat) és a summary.json (sourceSpans.utteranceIds +
// a span-idők, sourceSpeakers kulcsai) kapja; a transcript.embeddings.bin törlődik.
//
// A `tanara-cli reslice` használja. UI-független, nincs QObject.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerOverlay.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::reslice {

// A tokens.json gyökerébe írt jelölő: ez az átirat már korrigált időzítésű.
inline QString timingMarkerKey() { return QStringLiteral("timing"); }
inline QString timingMarkerValue() { return QStringLiteral("soniox-corrected"); }

enum class Verdict {
    Error,              // nincs / olvashatatlan tokens.json, nem leképezhető sor
    RealWordSpans,      // a token-szélességek mediánja > 80 ms: valódi szó-tartományok (Whisper)
    AlreadyCorrected,   // a tokens.json-on már ott a jelölő
    Reslice,            // van mit tenni
};

struct Plan {
    Verdict verdict = Verdict::Error;
    QString error;
    qint64  medianWidthMs = -1;
    int     tokenCount = 0;

    QVector<TranscriptLine> oldLines;   // a mostani segments.json (vagy a tokenekből, ha hiányzik)
    QVector<TranscriptLine> newLines;   // az újraszeletelt sorok, új id-vel
    QVector<int> mapping;               // oldLines[i] → newLines index
    QMap<QString, QString> idMap;       // régi id → új id (minden régi sor)
    int  renamedIds = 0;                // ahol az id változik
    int  mergedLines = 0;               // új sorok, amelyekbe több régi sor olvadt
    int  textChanged = 0;               // 1:1 párok, ahol a szöveg eltér (nem várt)
    // Kezdet-eltolás (régi kezdet − új kezdet, ms) hisztogramja: "0", "1-100", "101-200",
    // "201-300", ">300".
    QMap<QString, int> shiftHistogram;

    bool overlayPresent = false;
    bool overlayStale = false;          // az overlay egy korábbi átirathoz tartozik → nem nyúlunk hozzá
    int  overlayRefs = 0;               // id-hivatkozások száma az overlay-ben
    int  overlayRenamed = 0;            // ebből átnevezendő
    int  overlayConflicts = 0;          // két régi sor bejegyzése ugyanarra az új id-re kerülne (eltérő tartalommal)

    bool summaryPresent = false;
    int  summaryRefs = 0;
    int  summaryRenamed = 0;

    bool embeddingsPresent = false;

    MergedTranscript corrected;         // a korrigált tokenek (a mentéshez)
};

// Elemzés, írás nélkül.
Plan plan(const QString& meetingFolder);

struct ApplyResult {
    bool ok = false;
    QString error;
    QStringList backups;        // most készült .bak fájlok (a meglévőt nem írja felül)
    QStringList written;        // átírt fájlok
    bool embeddingsRemoved = false;
    bool markdownRegenerated = false;
};

// A plan alkalmazása (Verdict::Reslice kell). overlayConflicts > 0 esetén csak force-szal
// (akkor a korábbi régi sor bejegyzése nyer). Előbb a .bak másolatok
// (transcript.tokens.json / .segments.json / .speakers.json / summary.json / transcript.md),
// aztán az írás.
ApplyResult apply(const QString& meetingFolder, const Plan& plan, bool force = false);

} // namespace tanara::reslice
