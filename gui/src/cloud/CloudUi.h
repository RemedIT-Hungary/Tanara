#pragma once
// Tanara Cloud shared GUI helpers: money / hours formatting in the UI language, the error id
// row (copyable), and the error dialogs (402, 426, 503/429/401/403, general and partial
// errors). All strings go through tr().
#include "tanara/cloud/CloudTypes.h"

#include <QString>
#include <functional>

class QWidget;
class QLayout;
class QLabel;

namespace tanara { class AppController; }

namespace tanara_gui::cloudui {

QString lang();                                   // the active UI language (hu | en)
QString money(const tanara::Money& m, tanara::MoneyStyle style = tanara::MoneyStyle::Balance);
QString hours(double h);                          // "2.7 hours" / "45 minutes"
// The tier's human-readable name: Fast / Accurate (empty: Expert).
QString tierName(const QString& tier);

// Error id row: "Error ID: req_... [Copy]" + "We cannot see the meeting's content."
QWidget* requestIdRow(const QString& requestId, QWidget* parent);

void openUrl(const QString& url);

// Muted (secondary) text in the theme's placeholder color: readable contrast on both light
// and dark themes (palette(mid) is too faint).
void mute(QWidget* w);

// What the caller should do after the dialog.
enum class ErrorAction { None, Retry, Continue, Login, Topup, Terms, Update };

// A cloud error dialog (modal). kind: transcribe | summary | topics | complex | estimate |
// account. chargedSoFar > 0 means a partial error: "The parts completed so far cost $X. You
// can continue..." and then we do NOT say "We did not charge anything".
// canContinue: whether the "Continue" button makes sense (complex summary).
ErrorAction showCloudError(QWidget* parent, tanara::AppController* app, const tanara::CloudError& e,
                           const QString& kind, const tanara::Money& chargedSoFar = {},
                           bool canContinue = false);

// A short, one-line text for an error (for bars, inline states).
QString shortErrorText(const tanara::CloudError& e);

// The top-up path: top-up link opens the browser; if there is no online top-up, "Write to
// us" (contact_url). The calling window should refresh the account when it regains focus.
void startTopup(QWidget* parent, tanara::AppController* app);

} // namespace tanara_gui::cloudui
