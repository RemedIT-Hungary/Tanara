#pragma once
//
// MeetingProfiles — megbeszélések hasonlósága beágyazó modell nélkül (az „alap” szint).
//
// Meetingenként egy PROFIL:
//  - résztvevők: a nevesített beszélők (speakerMap + a saját mikrofon-sáv neve), a könyvtárbeli
//    ritkaságukkal súlyozva (aki a megbeszélések több mint 60 %-án ott van, ~0 súlyú);
//  - cím-szavak: meetingnotes::titleWords;
//  - jellemző kifejezések (≤ 60): az átirat szavai hajtogatva, számok / rövid szavak /
//    töltelékszavak nélkül, könnyű magyar toldalék-levágással (tagtext::stem), súly =
//    tf × log(N / df) a könyvtáron. A ritka, egymástól legfeljebb egy betűben eltérő tövek
//    (félrehallás: „remedi” ~ „remedit”) összevonódnak; a megjelenített alak a leggyakoribb
//    eredeti írásmód.
//
// A sorok (meeting.json) betöltése és a kifejezés-számlálás (az átirat beolvasása, szavakra
// bontása) a „tanara-profiles” háttérszálon fut; a profil a meeting mappájába gyorsítótárazódik
// (profile.json, az átirat-fájl módosítási ideje + mérete szerint érvényes). A könyvtár-szintű
// statisztika (df, összevonás, súlyok) lustán, megváltoztathatatlan pillanatképként áll össze
// a kész profilokból — az építés végén a háttérszálon előre is.
//
// Szálbiztonság: az olvasó metódusok (similar, similarToDraft, termsOf, topTerms, sharedTerms,
// participantWeight, folders, isBuilt) bármelyik szálról hívhatók; az ensureBuilt /
// invalidate / isIdle a tulajdonos (fő) szálé. Az első ensureBuilt után (háttér-mód; az
// alkalmazás indításkor hívja) a fő szál sosem tölt be sorokat és sosem számol statisztikát:
// elavult állapotnál a korábbi pillanatképet kapja, a háttérszál pedig frissít. Előtte (csak
// szinkron használat: tesztek, egyszerű eszközök) az első olvasás a fő szálon tölt és számol.
//
// Hasonlóság: 0.45 · résztvevők (súlyozott Jaccard) + 0.40 · kifejezések (koszinusz) +
// 0.15 · cím (meetingnotes::titleSimilarity).
//
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

class MeetingStore;

namespace tagtext {

// Hajtogatott (ékezet nélküli, kisbetűs) szó töve: a leghosszabb illeszkedő toldalék levágva,
// egyszer, csak ha a tő legalább 4 betű marad.
QString stem(const QString& foldedWord);

// Töltelék- / kötőszó-e (hajtogatott alakban; magyar és angol).
bool isStopWord(const QString& foldedWord);

// Egy szöveg kifejezés-jelöltjei: (tő, eredeti szóalak) párok, a szöveg sorrendjében.
QVector<QPair<QString, QString>> terms(const QString& text);

} // namespace tagtext

class MeetingProfiles : public QObject {
    Q_OBJECT
public:
    explicit MeetingProfiles(MeetingStore* store, QObject* parent = nullptr);
    ~MeetingProfiles() override;

    // A sorok betöltése és a hiányzó / elavult profilok építése a háttérszálon (nem blokkol;
    // a fő szálon csak az index-lekérdezés fut). profileReady jelek, a kör végén idle() —
    // akkor is, ha nem volt teendő. A még el nem kezdett kör nem duplázódik.
    void ensureBuilt();
    bool isIdle() const;
    bool isBuilt(const QString& meetingId) const;
    // Egy meeting profiljának eldobása (pl. új átirat); a következő ensureBuilt újraépíti.
    void invalidate(const QString& meetingId);

    // A legjobban hasonlító MÁSIK megbeszélések, csökkenő pontszám szerint (0 pontszám kimarad).
    QVector<SimilarHit> similar(const QString& meetingId, int limit = 8) const;
    // Még nem létező megbeszéléshez (import / felvétel előtt): cím + résztvevők alapján.
    QVector<SimilarHit> similarToDraft(const QString& title, const QStringList& participants,
                                       const QString& excludeId = QString(), int limit = 8) const;

    // Megjelenítéshez: a meeting jellemző kifejezései, ill. több meeting közös jellemzői.
    QStringList termsOf(const QString& meetingId, int limit = 12) const;
    QStringList topTerms(const QStringList& meetingIds, int limit = 12) const;
    // Két meeting közös jellemző kifejezései (a súlyok szorzata szerint).
    QStringList sharedTerms(const QString& a, const QString& b, int limit = 3) const;
    // A meeting résztvevőinek ritkasági súlya (0..1) — teszthez / indokláshoz.
    double participantWeight(const QString& name) const;
    // Az ismert meetingek mappái (id → mappa) — háttérszálról az index nélkül (EmbeddingIndex).
    QHash<QString, QString> folders() const;

signals:
    void profileReady(QString meetingId);
    void idle();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
