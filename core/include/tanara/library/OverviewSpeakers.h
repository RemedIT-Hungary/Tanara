#pragma once
//
// OverviewSpeakers (D5): the Overview's "who separated the speakers" facts, turned into the texts and flags of the
// processing panel's transcript step (OverviewSnapshot::speakers). Pure: the real facade feeds it the facts of
// app::Workflows::SpeakerStatus, the fictional one and the tests feed it hand-made ones.
//
//   source "provider"  -> "Beszélők: a szolgáltatótól"
//   source "local"     -> "Beszélők: helyi elkülönítés"
//   source "none"      -> "Beszélők: nincs — kapcsold be a Beállításokban" (the setting is off) /
//                         "Beszélők: nincs — tölts le modellt a Beállításokban" (a model is missing), both with
//                         fixTarget "diarization" (Settings > Processing > Speaker separation);
//                         "Beszélők: nincs szétválasztva" when it could run
//   a failed run       -> "Beszélők: a felismerés nem sikerült — <reason>" (the step needs attention)
//
#include <QString>
#include <QVariantMap>

namespace tanara::library {

// What the processing panel needs to know (the plain copy of app::Workflows::SpeakerStatus).
struct SpeakerFacts {
    QString source;          // "provider" | "local" | "none"
    QString blocker;         // source "none": "disabled" | "model" | ""
    QString problem;         // the last failed local diarization's detail ("" = none)
    bool canDiarize = false;
    bool hasNames = false;
    int  speakerCount = 0;   // the remembered "How many speakers?" (0 = automatic)
};

// The human (Hungarian) reason of a failed local diarization from the core's English detail / error code.
QString diarizationProblemText(const QString& raw);

// OverviewSnapshot::speakers for a transcript: { source, line, fixTarget, canDiarize, hasNames, speakerCount,
// running, failed, problem, attention }. attention: the transcript step must show the attention state
// (a failed run, or no usable model for a provider without speakers).
QVariantMap overviewSpeakers(const SpeakerFacts& facts, bool diarizeRunning);

} // namespace tanara::library
