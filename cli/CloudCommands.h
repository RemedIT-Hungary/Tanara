#pragma once
// tanara-cli: Tanara Cloud commands (see CloudCommands.cpp).
#include <QString>
#include <QStringList>

namespace tanara { class AppController; }

// `cloud <subcommand> ...`; returns the exit code.
int runCloudCommand(tanara::AppController& app, const QStringList& args);
// Prints the estimate. enoughOut: whether the job can start (balance is sufficient).
bool printCloudEstimate(tanara::AppController& app, const QString& meetingId, const QString& task,
                        const QString& mode, bool* enoughOut);
