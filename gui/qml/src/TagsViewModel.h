#pragma once
//
// TagsViewModel — a Címkék ablak (C08, T10–T13; TagsWindow.qml) nézetmodellje, a
// PeopleViewModel mintájára.
//
// Bal oldal: `tags` (TagListModel) — keresés a nevekben (ékezet- és kisbetű-független, a
// találat kiemelve), rendezés (legutóbb használt / ABC / leggyakoribb). Jobb oldal: a
// kijelölt címke (`selectedId`) tanult profilja (`detail`): résztvevők, kifejezések, együtt
// járó címkék, megbeszélések. Műveletek: átnevezés, összevonás, törlés — mind visszavonható
// lépés a backendben; utánuk `toast(szöveg, visszavonható)`.
//
// Controller nélkül a kitalált 12 címke; `demoState`: T10 | T11 | T12 | T13 | rename.
//
#include "TagListModel.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TagBackend.h"

namespace tanara_qml {

class TagsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    Q_PROPERTY(tanara_qml::TagListModel* tags READ tags CONSTANT)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY filterChanged)
    // "recent" | "alpha" | "count"
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY sortChanged)
    Q_PROPERTY(int count READ count NOTIFY listChanged)            // látható sorok
    Q_PROPERTY(int totalCount READ totalCount NOTIFY listChanged)  // a teljes készlet
    Q_PROPERTY(QString countText READ countText NOTIFY listChanged)

    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // { id, name, meta, meetingCount, participants: [{ name, monogram, countText }], terms: [..],
    //   cooccurring: [{ id, name, countText }], meetings: [{ meetingId, title, dateText,
    //   durationText }], meetingsMeta }
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toast)

public:
    explicit TagsViewModel(QObject* parent = nullptr);

    QObject* controller() const { return m_controller; }
    void setController(QObject* controller);
    void setBackend(TagBackend* backend);           // tesztekhez; nem veszi át
    TagBackend* backend() const { return m_backend; }
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    TagListModel* tags() { return &m_list; }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    bool searching() const { return !m_filter.trimmed().isEmpty(); }
    QString sort() const { return m_sort; }
    void setSort(const QString& sort);
    QString sortLabel() const;
    int count() const { return m_list.count(); }
    int totalCount() const { return int(m_all.size()); }
    QString countText() const;

    QString selectedId() const { return m_selected; }
    void setSelectedId(const QString& id);
    int selectedRow() const { return m_list.indexOfId(m_selected); }
    bool hasSelection() const { return !m_selected.isEmpty(); }
    QVariantMap detail() const { return m_detail; }

    bool canUndo() const { return m_backend && m_backend->canUndo(); }
    QString undoLabel() const { return m_backend ? m_backend->undoLabel() : QString(); }
    QString toastText() const { return m_toastText; }

    Q_INVOKABLE QString tagName(const QString& id) const;
    // Hibaüzenet, vagy üres, ha sikerült.
    Q_INVOKABLE QString rename(const QString& id, const QString& name);
    // Jelöltek: előbb a hasonló nevűek, aztán a gyakran együtt járók: [{ id, name, meta }].
    Q_INVOKABLE QVariantList mergeCandidates(const QString& id) const;
    // Az eredmény-sor (StyledText): „Eredmény: <b>16 megbeszélés</b> (1 közös). …”
    Q_INVOKABLE QString mergeResultText(const QString& fromId, const QString& keepId) const;
    Q_INVOKABLE QString merge(const QString& fromId, const QString& keepId);
    Q_INVOKABLE QString deleteTitle(const QString& id) const;
    Q_INVOKABLE QString deleteText(const QString& id) const;
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void undo();

signals:
    void controllerChanged();
    void demoStateChanged();
    void filterChanged();
    void sortChanged();
    void listChanged();
    void selectionChanged();
    void detailChanged();
    void undoChanged();
    void toast(const QString& text, bool undoable);

private:
    void attach(TagBackend* backend);
    void rebuild();
    void refreshDetail();
    void step(const QString& label);
    const TagItem* find(const QString& id) const;

    QPointer<QObject> m_controller;
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QList<QMetaObject::Connection> m_conns;
    QString m_demoState;

    TagListModel m_list;
    QVector<TagItem> m_all;
    QString m_filter;
    QString m_sort = QStringLiteral("recent");
    QString m_selected;
    QVariantMap m_detail;
    QString m_toastText;
};

// A határozott névelő egy név elé („a #Nordvik”, „az #Ügyféltámogatás”) — a magyar szövegekhez.
QString tagArticle(const QString& name);

} // namespace tanara_qml
