#pragma once
//
// LibraryPendingModel — az M02 („Válassz egy megbeszélést / Ezek várnak rád”) kártya sorai a
// MeetingLibrary::pendingItems-ből: átírásra váró felvételek (összevonva egy sorba), elavult
// összefoglalók és sikertelen átírások (megbeszélésenként egy sor, legfeljebb pár darab).
// Controller nélkül (App.demo) a kitalált mintakönyvtárból dolgozik.
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

    // A lista összeállítása a core elemeiből (statikus: tesztelhető).
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
