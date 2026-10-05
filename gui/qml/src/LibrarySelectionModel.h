#pragma once
//
// LibrarySelectionModel — a könyvtár többes kijelölése és a közös címkézés (C05, T05).
//
//  - Kijelölés: Ctrl+kattintás → toggle(id), Shift+kattintás → rangeTo(id) (a horgonytól a
//    lista látható sorrendjében), Esc → clear(). Az első Ctrl+kattintás a megnyitott
//    megbeszélést (currentId) is a kijelölésbe veszi, így két elemmel indul.
//  - A jobb oldali panel adatai: a kijelölt megbeszélések (cím, címkék, dátum) és a közös
//    címkék sorai (mindegyiken / csak némelyiken, „3/3” / „1/3”).
//  - Tömeges címkézés: addTagToAll / removeTagFromAll. Controllerrel a TagService-en át
//    (beginGroup → bulkAdd / bulkRemove → endGroup), így EGY lépésben visszavonható;
//    controller nélkül (demó) a mintakönyvtár memóriabeli címkéit módosítja.
//
// A sorrendhez és a sorok adataihoz a LibraryListModel-t használja (`library`).
//
#include "LibraryListModel.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class LibrarySelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(LibraryListModel* library READ library WRITE setLibrary NOTIFY libraryChanged)
    // A megnyitott megbeszélés (az első Ctrl+kattintás ezzel együtt jelöl ki).
    Q_PROPERTY(QString currentId READ currentId WRITE setCurrentId NOTIFY currentIdChanged)
    Q_PROPERTY(QStringList ids READ ids NOTIFY selectionChanged)
    Q_PROPERTY(int count READ count NOTIFY selectionChanged)
    // A kijelölt megbeszélések a lista sorrendjében: [{ id, title, tagsText, dateText }].
    Q_PROPERTY(QVariantList items READ items NOTIFY contentChanged)
    // A közös címkék: [{ id, name, onCount, total, full, countText, note }]
    //   full: mindegyiken rajta van; countText: „3/3” / „1/3”; note: a chip melletti magyarázat.
    Q_PROPERTY(QVariantList tagRows READ tagRows NOTIFY contentChanged)
    // A mindegyiken rajta lévő címkék (a beviteli mező ezeket már nem kínálja).
    Q_PROPERTY(QStringList fullTagIds READ fullTagIds NOTIFY contentChanged)

public:
    explicit LibrarySelectionModel(QObject* parent = nullptr);

    // A controller alapból az App-singletoné; tesztben injektálható.
    void setController(tanara::AppController* controller);

    LibraryListModel* library() const { return m_library; }
    void setLibrary(LibraryListModel* library);
    QString currentId() const { return m_currentId; }
    void setCurrentId(const QString& id);
    QStringList ids() const { return m_ids; }
    int count() const { return int(m_ids.size()); }
    QVariantList items() const { return m_items; }
    QVariantList tagRows() const { return m_tagRows; }
    QStringList fullTagIds() const;

    Q_INVOKABLE bool contains(const QString& id) const { return m_ids.contains(id); }
    Q_INVOKABLE void toggle(const QString& id);
    Q_INVOKABLE void rangeTo(const QString& id);
    Q_INVOKABLE void clear();
    // Kijelölés kívülről (demó-állapot, teszt).
    Q_INVOKABLE void selectIds(const QStringList& ids);
    // Név vagy azonosító; név esetén szükség szerint létrehozza. Egy visszavonható lépés.
    Q_INVOKABLE void addTagToAll(const QString& nameOrId);
    Q_INVOKABLE void removeTagFromAll(const QString& tagId);

signals:
    void libraryChanged();
    void currentIdChanged();
    void selectionChanged();
    void contentChanged();

private:
    void setIds(const QStringList& ids);
    void rebuild();
    QStringList orderedIds() const;    // a lista sorrendjében (ami nem látszik, a végén)

    QPointer<LibraryListModel> m_library;
    QPointer<tanara::AppController> m_controller;
    bool m_controllerInjected = false;
    QString m_currentId;
    QString m_anchor;
    QStringList m_ids;
    QVariantList m_items;
    QVariantList m_tagRows;
};

} // namespace tanara_qml
