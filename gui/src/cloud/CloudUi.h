#pragma once
//
// Tanara Cloud — közös GUI-segédek: pénz/óra formázás a UI nyelvén, hibaazonosító-sor
// (másolható), és a hiba-dialógusok (K-09 402, K-10 426, K-11 503/429/401/403, K-12 általános
// és részleges hiba). A szövegek a design-brief + kiegészítések szerint; minden tr()-rel.
//
#include "tanara/cloud/CloudTypes.h"

#include <QString>
#include <functional>

class QWidget;
class QLayout;
class QLabel;

namespace tanara { class AppController; }

namespace tanara_gui::cloudui {

QString lang();                                   // az aktív UI-nyelv (hu | en)
QString money(const tanara::Money& m, tanara::MoneyStyle style = tanara::MoneyStyle::Balance);
QString hours(double h);                          // „2,7 óra” / „45 perc”
// A tier ember-olvasható neve: Gyors / Pontos (üres → Expert).
QString tierName(const QString& tier);

// Hibaazonosító-sor: „Hibaazonosító: req_… [Másolás]” + „A meeting tartalmát nem látjuk.”
QWidget* requestIdRow(const QString& requestId, QWidget* parent);

void openUrl(const QString& url);

// Halvány (másodlagos) szöveg a téma helykitöltő-színével — olvasható kontraszt világos és
// sötét témán is (a palette(mid) túl halvány).
void mute(QWidget* w);

// Mit tegyen a hívó a dialógus után.
enum class ErrorAction { None, Retry, Continue, Login, Topup, Terms, Update };

// Egy cloud-hiba dialógusa (modális). kind: transcribe | summary | topics | complex | estimate |
// account. chargedSoFar > 0 → részleges hiba (K-12): „Az eddig elkészült részek díja: $X.
// Folytathatod…” — ilyenkor NEM írjuk, hogy „Nem terheltünk semmit”.
// canContinue: van-e értelme a „Folytatás” gombnak (komplex összefoglaló).
ErrorAction showCloudError(QWidget* parent, tanara::AppController* app, const tanara::CloudError& e,
                           const QString& kind, const tanara::Money& chargedSoFar = {},
                           bool canContinue = false);

// Rövid, egysoros szöveg egy hibához (sávokhoz, inline állapotokhoz).
QString shortErrorText(const tanara::CloudError& e);

// A feltöltés útja (K-09/K-08/K-02): topup-link → böngésző; ha nincs online feltöltés (P0),
// „Írj nekünk” (contact_url). A hívó ablak újra-fókuszakor érdemes a fiókot frissíteni.
void startTopup(QWidget* parent, tanara::AppController* app);

} // namespace tanara_gui::cloudui
