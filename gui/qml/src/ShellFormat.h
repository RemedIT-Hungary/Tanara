#pragma once
//
// Tanara QML — a héj (könyvtár, fejléc, lejátszó) közös szöveg-formázói: dátum, hossz,
// lejátszási idő, állapot-ikon kulcsok. Tiszta függvények, állapot nélkül.
//
#include "tanara/jobs/JobTypes.h"

#include <QDateTime>
#include <QLocale>
#include <QString>

namespace tanara_qml::fmt {

// A felület nyelvének megfelelő locale (hu / en) — a dátumok hónapneveihez.
QLocale uiLocale();

// Könyvtár-elem dátuma: „okt. 2.” (en: „Oct 2”).
QString shortDate(const QDateTime& dt);
// Fejléc dátuma: „2026. okt. 1.” (en: „Oct 1, 2026”).
QString longDate(const QDateTime& dt);
// Könyvtár-elem hossza: „30 p”, „1 ó 16 p”; egy perc alatt „<1 p”.
QString shortDuration(qint64 ms);
// Fejléc / lejátszó ideje: „30:34”, „1:16:04” (padMinutes: „00:17” a lejátszóban).
QString clock(qint64 ms, bool padMinutes = false);

// A TStatusIcon `state` kulcsa egy lépés-állapotból: done | running | error | missing.
QString stepStateKey(tanara::StepState state);

} // namespace tanara_qml::fmt
