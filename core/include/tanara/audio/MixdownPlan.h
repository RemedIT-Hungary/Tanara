#pragma once
//
// Tanara — a lekeverés (mixdown.mp3) terve: mely sávfájlok kerülnek bele, mennyi eltolással,
// és milyen ffmpeg-argumentumokkal. Tiszta (I/O csak a fájlok meglétének ellenőrzése), így
// a szűrőgráf tesztelhető; a futtatás az AppController::regenerateMixdown dolga.
//
// Eltolás: a később kezdődő sáv (Track::startOffsetMs > 0 — felvétel közben bekapcsolt eszköz,
// egy eszköz második szakasza, `align`-nal igazított sáv) adelay-jel kerül a helyére, így a
// keverékben minden hang a megbeszélés-idő szerinti helyén szól.
//
#include "tanara/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct MixdownInput {
    QString trackId;
    QString path;          // abszolút út
    qint64  offsetMs = 0;  // ennyi csenddel tolva kerül a keverékbe
};

struct MixdownPlan {
    QVector<MixdownInput> inputs;   // a bevont (Track::included), lemezen meglévő sávok (a meeting.json sorrendjében)
    QStringList missing;            // az aktív, de hiányzó fájlú sávok megjelenített neve

    // A meeting aktív sávjaiból (az eldobottak kimaradnak).
    static MixdownPlan fromMeeting(const Meeting& m);

    // A keverő szűrő: üres inputs-ra üres. Egy eltolás nélküli bemenetnél csak a normalizálás
    // („-af”), különben „-filter_complex” adelay-ekkel és amix-szel. Visszatérés: a két
    // argumentum (kapcsoló + gráf).
    static QStringList filterArgs(const QVector<qint64>& offsets);

    // A teljes ffmpeg-argumentumlista (a program neve nélkül): bemenetek, szűrő, MP3-kódolás,
    // haladás a stdoutra (-progress pipe:1), kimenet. inputs üres → üres lista.
    QStringList ffmpegArgs(const QString& outPath) const;
};

} // namespace tanara
