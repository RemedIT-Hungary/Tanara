#pragma once
//
// LibraryPendingModel: the rows of the "Pick a meeting / Waiting for you" card from
// MeetingLibrary::pendingItems: recordings awaiting transcription (merged into one row), stale
// summaries and failed transcriptions (one row per meeting, at most a few).
// Without a controller (App.demo) it works from the fictional sample library.
//
#include "tanara/library/MeetingLibrary.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class LibraryPendingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // [{ kind: "awaiting"|"stale"|"failed", meetingId, title, subtitle, actionLabel,
    //    iconName, tone: "accent"|"warn"|"danger", tab }]
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit LibraryPendingModel(QObject* parent = nullptr);

    void setController(tanara::AppController* controller);
    QVariantList items() const { return m_items; }

    // Assembling the list from the core's items (static: testable).
    static QVariantList build(const QVector<tanara::PendingItem>& pending);

signals:
    void itemsChanged();

private:
    void attach();
    void reload();

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::MeetingLibrary> m_library;
    bool m_controllerInjected = false;
    QVariantList m_items;
    QTimer m_reloadTimer;
};

} // namespace tanara_qml
