#pragma once
//
// TranscriptReslice — egy MEGLÉVŐ (Soniox-féle időzítésű) átirat sorainak párnázása a
// transcript.tokens.json-ból, az utterance id-k átvezetésével (a `tanara-cli reslice` magja).
//
// A csoportosítás az EREDETI token-időkkel fut (ugyanaz a merger), csak a kész sorok kapnak
// elő-/utó-futást (sonioxtiming::padUtterances) → a sorok száma és sorrendje nem változik, a
// régi → új id térkép index szerinti 1:1. A megszólalás-id a kezdőidőből képződik („u<startMs>"),
// ezért a párnázás az id-ket megváltoztatja: a térképet az overlay (transcript.speakers.json:
// minden id-hivatkozás + az átirat-ujjlenyomat) és a summary.json (sourceSpans.utteranceIds + a
// span-idők, sourceSpeakers kulcsai) kapja; a transcript.embeddings.bin törlődik. A tokens.json
// tokenjei érintetlenek; a gyökérbe "timing": "emission" kerül (ettől párnáz a merger az app
// minden szeletelésénél — és ez a második futás „nincs teendő" jelzője is).
//
// UI-független, nincs QObject.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerOverlay.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::reslice {

// A tokens.json gyökérmezője (ugyanazt írja az AppController a Soniox-átiratokhoz).
inline QString timingKey() { return QStringLiteral("timing"); }
inline QString emissionTimingValue() { return QStringLiteral("emission"); }

enum class Verdict {
    Error,              // nincs / olvashatatlan tokens.json, vagy a segments.json nem illik hozzá
    RealWordSpans,      // a token-szélességek mediánja > 80 ms: valódi szó-tartományok (Whisper)
    AlreadyPadded,      // a tokens.json-on már ott a "timing": "emission"
    Reslice,            // van mit tenni
};

struct Plan {
    Verdict verdict = Verdict::Error;
    QString error;
    qint64  medianWidthMs = -1;
    int     tokenCount = 0;

    QVector<TranscriptLine> oldLines;   // a mostani segments.json (vagy a tokenekből, ha hiányzik)
    QVector<Utterance>      newSegments;// a párnázott sorok (a segments.json új tartalma)
    QVector<TranscriptLine> newLines;   // ugyanezek, új id-vel; oldLines[i] → newLines[i]
    QMap<QString, QString> idMap;       // régi id → új id (minden régi sor)
    int  renamedIds = 0;                // ahol az id változik
    int  textChanged = 0;               // ahol a szöveg eltér (nem várt; a szöveg nem változik)
    // Kezdet-eltolás (régi kezdet − új kezdet, ms) hisztogramja: "0", "1-100", "101-200",
    // "201-300", ">300" (a párnázás legfeljebb 300 ms).
    QMap<QString, int> shiftHistogram;

    bool overlayPresent = false;
    bool overlayStale = false;          // az overlay egy korábbi átirathoz tartozik → nem nyúlunk hozzá
    int  overlayRefs = 0;               // id-hivatkozások száma az overlay-ben
    int  overlayRenamed = 0;            // ebből átnevezendő

    bool summaryPresent = false;
    int  summaryRefs = 0;
    int  summaryRenamed = 0;

    bool embeddingsPresent = false;
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

// A plan alkalmazása (Verdict::Reslice kell). Előbb a .bak másolatok (transcript.tokens.json /
// .segments.json / .speakers.json / summary.json / transcript.md), aztán az írás.
ApplyResult apply(const QString& meetingFolder, const Plan& plan);

} // namespace tanara::reslice
