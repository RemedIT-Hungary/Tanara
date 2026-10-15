#pragma once
//
// OverviewSummaryStep: the Overview processing panel's "Összefoglaló" (summary) step, from plain facts. Pure: the
// real facade feeds it the meeting's state, the fictional one and the tests feed it hand-made facts.
//
// phase (what the step's button area shows):
//   "running"       the summary is being made: progress + "Mégse"
//   "noTranscript"  nothing to summarize yet: waiting, the button is disabled ("az átirat után")
//   "blocked"       no language model / key set up: attention, "Beállítások megnyitása" (fixTarget "services")
//   "failed"        the last attempt failed: attention, the message + the technical detail, "Újra"
//   "done"          a current summary exists: "Újra" (secondary) + the Summary tab link
//   "stale"         a summary exists but the transcript changed since: attention, same buttons as "done"
//   "ready"         can start: waiting, the primary "Összefoglaló készítése"
//
#include <QString>
#include <QVariantMap>

namespace tanara::library {

struct SummaryStepFacts {
    bool hasTranscript = false;
    bool hasSummary = false;
    bool stale = false;
    bool running = false;
    int  percent = -1;            // running: 0..100, -1 = unknown
    bool blocked = false;         // the provider / key is missing (readiness blocker, not the meeting state)
    QString blockerDetail;        // blocked: what is missing, human text
    QString errorMessage;         // the last failed attempt ("" = none)
    QString errorDetail;          // its technical line
    bool errorFixInSettings = false;   // the error can be fixed in the Settings (address / key)
};

// The step map: { key "summary", title, sub, state (done|running|attention|waiting), parallel false, progress,
// phase, canStart, fixTarget, note, detail }. fixTarget "services" for the blocked step and for a
// failed one whose error is fixable in the Settings. progress: -1 none, -2 indeterminate, 0..100.
QVariantMap overviewSummaryStep(const SummaryStepFacts& facts);

} // namespace tanara::library
