#pragma once
//
// LibraryPendingModel: the rows of the "Pick a meeting / Waiting for you" card from
// ILibraryFacade::pendingItems: recordings awaiting transcription (merged into one row), stale
// summaries and failed transcriptions (one row per meeting, at most a few).
//
#include "tanara/library/MeetingLibrary.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara::library { class ILibraryFacade; }

namespace tanara_qml {

class LibraryPendingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // [{ kind: "awaiting"|"stale"|"failed", meetingId, title, subtitle, actionLabel,
    //    iconName, tone: "accent"|"warn"|"danger", tab }]
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    // From QML: follows the App singleton's services (AppContext::services().library).
    explicit LibraryPendingModel(QObject* parent = nullptr);
    // With a given facade (tests, hosts); it is not taken over.
    explicit LibraryPendingModel(tanara::library::ILibraryFacade* facade, QObject* parent = nullptr);
    QVariantList items() const { return m_items; }

    // Assembling the list from the core's items (static: testable).
    static QVariantList build(const QVector<tanara::library::PendingItem>& pending);

signals:
    void itemsChanged();

private:
    void init();
    void attach();
    void reload();

    QPointer<tanara::library::ILibraryFacade> m_facade;
    QPointer<tanara::library::ILibraryFacade> m_attached;
    bool m_injected = false;
    QVariantList m_items;
    QTimer m_reloadTimer;
};

} // namespace tanara_qml
