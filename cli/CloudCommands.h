#pragma once
// tanara-cli — Tanara Cloud parancsok (lásd CloudCommands.cpp).
#include <QString>
#include <QStringList>

namespace tanara { class AppController; }

// `cloud <alparancs> …` — visszatérés: kilépési kód.
int runCloudCommand(tanara::AppController& app, const QStringList& args);
// A becslés kiírása (K-06 szövegei). enoughOut: indítható-e (nem „nincs elég”).
bool printCloudEstimate(tanara::AppController& app, const QString& meetingId, const QString& task,
                        const QString& mode, bool* enoughOut);
