#pragma once
//
// Sáv-átiratok összefésülése egy időrendi, beszélőnként tagolt átiratba.
// A MergedTranscript::renderMarkdown() implementációja is itt (a .cpp-ben) él.
//
#include "tanara/Types.h"
#include <QVector>

namespace tanara {

// Összefűzi az összes sáv tokenjeit, beállítja minden tokenen a trackId/speaker
// mezőt a forrássáv alapján, stabilan startMs szerint rendez, és a nyelvet az
// első sávból veszi.
MergedTranscript mergeTranscripts(const QVector<TrackTranscript>& tracks);

// Beszéd-blokkok markdownja: bekezdésenként `[mm:ss]` **Beszélő** szöveg, üres sorral
// elválasztva. A MergedTranscript::renderMarkdown() és az összefoglaló részei is ezt használják.
QString renderUtterancesMarkdown(const QVector<Utterance>& utterances);

} // namespace tanara
