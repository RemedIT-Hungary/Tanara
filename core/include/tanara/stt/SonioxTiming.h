#pragma once
//
// A Soniox-féle (kiadási pillanat) időzítés kezelése: a kész SOROK utólagos párnázása. A
// TranscriptMerger (MergedTranscript::segments) és a `tanara-cli reslice` közös logikája.
// UI-független, szabad függvények, nincs QObject.
//
// Mérés (valódi meeting, 2026-10): a Soniox tokenjei ~60 ms szélesek, és a token KIADÁSÁNAK
// pillanatát jelölik (a szó vége felé), nem a hang kezdetét — pl. „R" 127410–127470, „ok"
// 127530–127590, „i." 127650–127710. A sor kezdete így 200–300 ms-mal a tényleges hangkezdet
// UTÁN van; rövid soroknál a szó nagy része kiesik a lejátszásból és a hang-beágyazásból is.
//
// Miért a sorokat, és nem a tokeneket: a token-szintű párnázás a tokenek közti szüneteket is
// szűkíti, így a merger (1,5 s szünet-küszöb) korábban külön sorokat vonna össze (mérve: 9 sor
// egy meetingen). A csoportosítás ezért az EREDETI token-időkkel fut, a tokens.json érintetlen
// marad; csak a kész sorok kezdete / vége mozdul. A Whisper-kompatibilis provider szó-időbélyegei
// valódi kezdet/vég párok — azokra nem fut.
//
#include "tanara/Types.h"

#include <QVector>

namespace tanara::sonioxtiming {

// A sor kezdete legfeljebb ennyivel kerül előrébb (az előző sor végéig).
constexpr qint64 kLeadMs = 300;
// A sor vége legfeljebb ennyivel kerül hátrébb (a következő sor kezdetéig).
constexpr qint64 kTailMs = 100;
// Soniox-féle (kiadási pillanat) időzítés: a token-szélességek mediánja legfeljebb ennyi.
constexpr qint64 kNarrowMedianMs = 80;

// A kész sorok párnázása (bármely beszélő; a sorrend startMs szerint, változatlan marad):
//   startMs = max(a KORÁBBAN kezdődő sorok legkésőbbi vége, startMs − kLeadMs), de nem
//             későbbi az eredetinél, és ≥ 0;
//   endMs   = min(a KÉSŐBB kezdődő sorok legkorábbi (új) kezdete, endMs + kTailMs), de nem
//             korábbi az eredetinél.
// Azonos kezdetű sorok egymást nem korlátozzák (ugyanannyit mozdulnak → az id-utótag marad).
// Ha a bemenet sorai nem fedik egymást, a kimenetéi sem; a sorrend nem változik.
void padUtterances(QVector<Utterance>& utterances);

// A token-szélességek (endMs − startMs) mediánja; üres listánál −1.
qint64 medianTokenWidthMs(const QVector<TranscriptToken>& tokens);

// Soniox-féle-e az időzítés (medián ≤ kNarrowMedianMs). Üres listánál false.
bool looksLikeEmissionTiming(const QVector<TranscriptToken>& tokens);

} // namespace tanara::sonioxtiming
