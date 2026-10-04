#pragma once
//
// A megbeszélés-megjegyzés (Meeting::contextNote) körüli, UI-független szabályok:
//
//  - Régi, automatikus megjegyzés: a figyelő korábban „Automatikusan észlelt hívás: <app>”
//    szöveget írt a megjegyzésbe. Ez nem a felhasználó szava, és az átírónak sem mond semmit:
//    betöltéskor üres megjegyzésként + észlelt hívás-alkalmazásként értelmezzük
//    (interpretLegacyNote), a következő mentés már a tiszta formát írja.
//  - Az átírónak küldött kontextus (fillSttContext): cím (general), a megjegyzés (text) és a
//    résztvevő-nevek (terms). Az automatikus hívás-szöveg sosem kerül bele.
//  - Sablon-javaslatok: a hasonló című (pl. ismétlődő „<Projekt> heti meeting”) korábbi
//    megbeszélések megjegyzései. A címeket szavanként hasonlítjuk, ékezet- és kisbetű-
//    függetlenül, a dátumokat és számokat figyelmen kívül hagyva (titleWords /
//    titleSimilarity). Pár száz meetingnél egy hívás mikroszekundumos nagyságrendű.
//
#include "tanara/Types.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct SttRequest;

namespace meetingnotes {

// „Automatikusan észlelt hívás: Teams” / „Automatically detected call: Teams” → true, app = „Teams”.
bool parseAutoCallNote(const QString& note, QString* app = nullptr);

// Ha a megjegyzés pontosan az automatikus mondat: a megjegyzés üres lesz, az app-név a
// detectedCallApp-be kerül (ha az még üres). true, ha változott valami.
bool interpretLegacyNote(Meeting& m);

// Az átírónak szánt szabad szöveg: a megjegyzés, vágva; az automatikus mondat sosem.
QString sttContextText(const Meeting& m);

// A context-envelope kitöltése a meetingből (general: cím, text: megjegyzés, terms: az aktív
// sávok fix beszélői).
void fillSttContext(SttRequest& req, const Meeting& m);

// A cím összehasonlítható szavai: ékezet nélkül, kisbetűvel, egyszer-egyszer; kimarad minden
// számot tartalmazó szó (dátum, sorszám, időpont), a hónap- és napnevek (rövidítve is), az
// egybetűs szavak és néhány névelő / kötőszó.
QStringList titleWords(const QString& title);

// Két szó-halmaz hasonlósága 0..1 (Jaccard: közös / összes). 0, ha a közös szavak mind
// „gyengék” (megbeszélés, meeting, heti, hívás, Teams …) — ezek egyedül nem tesznek két
// megbeszélést rokonná.
double titleSimilarity(const QStringList& a, const QStringList& b);

// A javaslathoz elég hasonló-e (titleSimilarity >= kSimilarThreshold).
constexpr double kSimilarThreshold = 0.5;
bool titlesSimilar(const QString& a, const QString& b);

// Egy jelölt a javaslatokhoz (a könyvtár gyorsítótárából; words = titleWords(title)).
struct NoteCandidate {
    QString     meetingId;
    QString     title;
    QDateTime   startedAt;
    QString     note;
    QStringList words;
};

struct NoteSuggestion {
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    QString   note;
    double    score = 0.0;
};

// Legfeljebb `limit` javaslat a `selfId` meetinghez: a hasonló című, NEM üres és a
// jelenlegitől (currentNote) eltérő megjegyzésű MÁSIK megbeszélések, legújabb elöl; azonos
// megjegyzés csak egyszer (a legújabb forrással).
QVector<NoteSuggestion> suggestNotes(const QVector<NoteCandidate>& candidates,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit = 3);

// Kényelmi változat közvetlenül meetingekből (tesztekhez, apró listákhoz).
QVector<NoteSuggestion> suggestNotes(const QVector<Meeting>& meetings,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit = 3);

} // namespace meetingnotes
} // namespace tanara
