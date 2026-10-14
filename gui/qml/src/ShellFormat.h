#pragma once
//
// Tanara QML: the shell's (library, header, player) shared text formatters: date, length,
// playback time, status icon keys. Pure functions, stateless.
//
#include "tanara/domain/Job.h"

#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml::fmt {

// The locale matching the UI language (hu / en), for the dates' month names.
QLocale uiLocale();

// A library item's date: "Oct 2" (hu: "okt. 2.").
QString shortDate(const QDateTime& dt);
// The header's date: "Oct 1, 2026" (hu: "2026. okt. 1.").
QString longDate(const QDateTime& dt);
// A library item's length: "30 min", "1 h 16 min"; under a minute "<1 min".
QString shortDuration(qint64 ms);
// The header / player time: "30:34", "1:16:04" (padMinutes: "00:17" in the player).
QString clock(qint64 ms, bool padMinutes = false);

// TStatusIcon's `state` key from a step state: done | running | error | missing.
QString stepStateKey(tanara::StepState state);

} // namespace tanara_qml::fmt

namespace tanara_qml {

// The formatters QML needs: the ONE playback / track time text (PlayerBar, ReviewFocusCard,
// TrackActivityStrip).  QML:  ShellFormat.clock(ms)  /  ShellFormat.clock(ms, true)
class ShellFormatBridge : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ShellFormat)
    QML_SINGLETON
public:
    using QObject::QObject;
    // "30:34", "1:16:04" (padMinutes: "00:17"); the milliseconds are floored to the second.
    Q_INVOKABLE QString clock(qint64 ms, bool padMinutes = false) const { return fmt::clock(ms, padMinutes); }
};

} // namespace tanara_qml
