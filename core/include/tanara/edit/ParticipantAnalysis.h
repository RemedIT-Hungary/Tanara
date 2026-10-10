#pragma once
//
// Átirat ELŐTTI hangelemzés („Ki volt ott?"): a sáv-aktivitás beszéd-szakaszaiból (TrackActivity)
// hang-ablakok, ablakonként beágyazás (VoiceEmbedderSet), sávonként agglomeratív klaszterezés,
// klaszterenként párosítás a tárolt lenyomatokkal → résztvevő-jelöltek bizonyítékokkal.
// Soniox / fizetős szolgáltatás nélkül, lokálisan.
//
// Két lépés, hogy a nehéz rész háttérszálon fusson:
//  1. computeVoiceClusters — dekódolás + beágyazás + klaszterezés (szálbiztos: csak a kapott
//     adatból dolgozik, tár-hozzáférés nincs; a PCM-olvasó cserélhető → teszt hamis hanggal);
//  2. buildParticipants — párosítás + bizonyítékok + csoportok (a fő szálon, a lenyomat-DB-vel
//     egy rangsoroló függvényen át).
// Az átirat megléte után bindRawSpeakers köti a klasztereket a nyers STT-beszélőkhöz időbeli
// átfedéssel; a kétoldalú nyers beszélő (mikrofon ÉS hívás) két résztvevőre bomlik.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/voiceid/EmbeddingSet.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

namespace tanara {

class VoiceEmbedderSet;

// Egy hang-ablak egy sávon (megbeszélés-időben).
struct SpeechWindow {
    QString trackId;            // a logikai sáv (vezető szakasz) id-ja
    QString side;               // "mic" | "loopback" | "" (egyéb)
    qint64  startMs = 0;
    qint64  endMs = 0;
    qint64 durationMs() const { return endMs - startMs; }
};

// Egy hang-klaszter: egy sávon egy beszélő (feltehetően).
struct VoiceCluster {
    QString trackId;
    QString side;
    QVector<QPair<qint64, qint64>> windows;   // a tagablakok [start, end) megbeszélés-időben
    EmbeddingSet centroid;                    // modellenként L2-normalizált centroid (nem perzisztált)
    QString sampleRef;                        // "<fájl>#start-end" — a medoid ablak
    // A párosítás eredménye (buildParticipants tölti):
    QString participantId;
    QString matchName;                        // az elfogadott lenyomat-találat (küszöb felett); üres = ismeretlen
    double  matchScore = -1.0;                // a legjobb találat pontszáma (küszöb alatt is)
    qint64 speechMs() const;
};

struct ClusterSet {
    QStringList modelIds;           // a fúzióban részt vevő modellek
    QVector<VoiceCluster> clusters;
    int windows = 0;                // hány ablakból (beágyazott) állt
    bool cancelled = false;
    QString error;                  // nem futott: miért (pl. nincs modell)
};

// Az elemzés perzisztált része (<mappa>/participants.analysis.json): a klaszterek ablakai és
// párosításai (a később érkező átirathoz kötéshez kellenek).
struct ParticipantAnalysisFile {
    QString at;
    QStringList modelIds;
    QString tracksKey;              // a bevont sávok lenyomata (activityFingerprint) az elemzéskor
    QVector<VoiceCluster> clusters;
    bool isEmpty() const { return at.isEmpty(); }
    static QString filePath(const QString& meetingFolder);
    static ParticipantAnalysisFile load(const QString& meetingFolder);
    bool save(const QString& meetingFolder) const;
};

namespace participants {

// Beszéd-szakasz: a beszéd-keretek (trackspeech::isSpeechFrame) futamai, legfeljebb
// kMaxGapMs szünettel összefogva. Ablak: legalább kMinWindowMs, legfeljebb kMaxWindowMs.
inline constexpr qint64 kMinWindowMs = 3000;
inline constexpr qint64 kMaxWindowMs = 10000;
inline constexpr qint64 kMaxGapMs = 400;
inline constexpr int    kMaxWindowsPerTrack = 60;
// Klaszter-összevonás (fúziós cosine) és lenyomat-párosítás küszöbe.
inline constexpr double kClusterThreshold = 0.55;
inline constexpr double kMatchThreshold = 0.5;
// Kézzel felvett résztvevő előnye a párosításnál (a pontszámhoz adódik).
inline constexpr double kManualBoost = 0.1;
// Kétoldalú nyers beszélő: mindkét oldal átfedése legalább ekkora hányad.
inline constexpr double kSplitMinShare = 0.25;

inline const QString kSideMic = QStringLiteral("mic");
inline const QString kSideLoopback = QStringLiteral("loopback");

QString sideOf(TrackKind kind);                 // Mic → "mic", Loopback → "loopback", más → ""
QString sideLabel(const QString& side);         // "mikrofon" | "hívás" | ""

// Egy sáv hang-ablakai (időrendben; túl sok esetén egyenletesen ritkítva kMaxWindowsPerTrack-re).
QVector<SpeechWindow> speechWindows(const TrackActivity& t, const QString& side);

// Agglomeratív klaszterezés cosine-centroiddal: ablakonként a klaszter címkéje (egy tag indexe).
QVector<int> clusterEmbeddings(const QVector<QVector<float>>& embs, double mergeThreshold);

// Egy logikai sáv [startMs, endMs) szakaszának hangja (16 kHz mono, [-1,1]); üres = nincs.
using PcmReader = std::function<QVector<float>(const QString& trackId, qint64 startMs, qint64 endMs)>;
// A valódi olvasó: a sáv (szakaszai) fájljából, az eltolással (tracktiming) — ffmpeg.
PcmReader filePcmReader(const Meeting& m);

// 1. lépés (háttérszál). A bevont sávok aktivitásából ablakok → beágyazás → sávonként klaszterek
// (az egyablakos klaszter zajnak számít, ha van más). progress(done,total) false → megszakítás.
ClusterSet computeVoiceClusters(const Meeting& m, const MeetingActivity& activity,
                                const VoiceEmbedderSet& embedders, const PcmReader& pcm,
                                const std::function<bool(int, int)>& progress = {});

// A jelöltek kontextusa (a fő szálon töltve).
struct CandidateContext {
    QString selfName;                                                  // a mikrofon gazdája
    std::function<QVector<VoiceMatch>(const EmbeddingSet&)> rank;      // lenyomat-rangsor, csökkenő
    std::function<bool(const QString& person)> hasVoiceprint;
    std::function<QStringList(const QString& person)> personTags;     // a személy címke-id-i
    std::function<QString(const QString& tagId)> tagName;             // megjelenítéshez; üres → id
};

// 2. lépés: a klaszterekből résztvevők. A meglévő kézi résztvevők megmaradnak (bizonyítékkal
// bővítve), a korábbi hang-jelöltek helyére újak jönnek; a jóváhagyott név jóváhagyott marad.
// A clusters participantId / matchName / matchScore mezőit kitölti.
QVector<Participant> buildParticipants(ClusterSet& clusters, const Meeting& m,
                                       const CandidateContext& ctx);

// A résztvevő bizonyítékai újraszámolva (heard: hallottuk-e; score: a hang-pontszám).
QVector<Evidence> participantEvidence(const Participant& p, bool heard, double score,
                                      const Meeting& m, const CandidateContext& ctx);

ParticipantGroup participantGroup(const Participant& p);
// A jóváhagyó párbeszéd alapértelmezése: névvel bír és nincs ellentmondó bizonyítéka.
bool defaultChecked(const Participant& p);

// Nyers beszélő-hivatkozás: "Beszélő 1" (egész) vagy "Beszélő 1@mic" / "@loopback" (kétoldalú
// nyers beszélő egyik oldala).
QString rawLabelOf(const QString& rawId);
QString rawSideOf(const QString& rawId);
QString rawId(const QString& rawLabel, const QString& side = QString());
// Megjelenítés: "Beszélő 1" ill. "Beszélő 1 · mikrofon" / "Beszélő 1 · hívás".
QString rawDisplay(const QString& rawId);

// A klaszterek kötése a nyers STT-beszélőkhöz időbeli átfedéssel (rawSpeakerIds, talkShare).
// Kétoldalú nyers beszélő (mindkét oldal ≥ kSplitMinShare, más-más legjobb résztvevővel) → két
// résztvevőre bontva ("@mic" / "@loopback"). true, ha bármi változott.
bool bindRawSpeakers(QVector<Participant>& ps, const QVector<VoiceCluster>& clusters,
                     const QVector<TranscriptLine>& lines);

// Egy átirat-sor oldala: a klaszter-ablakok átfedése, ennek híján a sáv-energia (activity lehet
// null). "" = eldönthetetlen.
QString lineSide(const TranscriptLine& line, const QVector<VoiceCluster>& clusters,
                 const MeetingActivity* activity);

// A jóváhagyás kötései: a bejelölt (accepted) résztvevők nyers beszélői a nevükre (kétoldalú
// nyers beszélőnél csak az adott oldal sorai), a csak ki nem jelölt résztvevőhöz kötött nyers
// beszélők névtelenre.
QVector<SpeakerBinding> approvalBindings(const QVector<Participant>& all, const QStringList& acceptedIds,
                                         const QVector<TranscriptLine>& lines,
                                         const QVector<VoiceCluster>& clusters,
                                         const MeetingActivity* activity);

} // namespace participants
} // namespace tanara
