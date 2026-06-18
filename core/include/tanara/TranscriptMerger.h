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

} // namespace tanara
