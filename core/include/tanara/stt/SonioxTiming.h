#pragma once
//
// A Soniox token-időzítésének korrekciója (a SonioxProvider és a `tanara-cli reslice` közös
// logikája). UI-független, szabad függvények, nincs QObject.
//
// Mérés (valódi meeting, 2026-10): a Soniox tokenjei ~60 ms szélesek, és a token KIADÁSÁNAK
// pillanatát jelölik (a szó vége felé), nem a hang kezdetét — pl. „R" 127410–127470, „ok"
// 127530–127590, „i." 127650–127710. A sor kezdete így 200–300 ms-mal a tényleges hangkezdet
// UTÁN van; rövid soroknál a szó nagy része kiesik a lejátszásból és a hang-beágyazásból is.
// A Whisper-kompatibilis provider szó-időbélyegei valódi kezdet/vég párok — azokra NEM kell.
//
#include "tanara/Types.h"

#include <QVector>

namespace tanara::sonioxtiming {

// A token kezdete legfeljebb ennyivel kerül előrébb (az előző token végéig).
constexpr qint64 kLeadMs = 300;
// A token vége legfeljebb ennyivel kerül hátrébb (a következő token kezdetéig).
constexpr qint64 kTailMs = 100;
// Soniox-féle (kiadási pillanat) időzítés: a token-szélességek mediánja legfeljebb ennyi.
constexpr qint64 kNarrowMedianMs = 80;

// EGY sáv tokenjeinek korrekciója, időrendben (a hívás stabilan rendez startMs szerint):
//   startMs = max(előző token endMs, startMs − kLeadMs), de nem későbbi az eredetinél, ≥ 0;
//   endMs   = min(következő token (új) startMs, endMs + kTailMs), de nem korábbi a startMs-nél.
// Az eredmény tokenjei nem fedik egymást.
void correctTrackTokens(QVector<TranscriptToken>& tokens);

// A token-szélességek (endMs − startMs) mediánja; üres listánál −1.
qint64 medianTokenWidthMs(const QVector<TranscriptToken>& tokens);

// Soniox-féle-e az időzítés (medián ≤ kNarrowMedianMs). Üres listánál false.
bool looksLikeEmissionTiming(const QVector<TranscriptToken>& tokens);

} // namespace tanara::sonioxtiming
