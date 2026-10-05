#pragma once
//
// MeetingTagsModel — egy megbeszélés címkesora (C03/C04, TagRow.qml) és „Miért?” panelje
// (TagWhyPopover.qml).
//
// tags: a felrakott címkék; suggestions: a még el nem fogadott, el nem utasított javaslatok
// (a backend legutóbbi eredményéből szűrve: ami már rajta van vagy elutasított, kiesik — így a
// visszavonás után magától visszajön). Elfogadás / elutasítás / „Mind” / eltávolítás egy-egy
// visszavonható lépés, a lépés neve (undoLabel) a toast szövege is.
//
// Kézi hozzáadás után a backendtől együtt járó javaslatot kér („<Címke> mellé gyakran”).
//
// Controller nélkül a kitalált készlet; `demoState` a T01 C03–C04 sorai:
// none | few | many | computing | similar | cooccur | llm, és why (a T02 panel adatai).
//
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "TagBackend.h"

namespace tanara_qml {

class MeetingTagsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // [{ id, name }] a megbeszélés sorrendjében
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    // [{ id, name, isNew, source }]
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    // "Javasolt" | "<Címke> mellé gyakran" | "Az összefoglaló alapján" ("" ha nincs javaslat)
    Q_PROPERTY(QString suggestionLabel READ suggestionLabel NOTIFY suggestionsChanged)
    // "similar" | "cooccur" | "llm" | ""
    Q_PROPERTY(QString suggestionSource READ suggestionSource NOTIFY suggestionsChanged)
    Q_PROPERTY(bool computing READ computing NOTIFY suggestionsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)

public:
    explicit MeetingTagsModel(QObject* parent = nullptr);

    QObject* controller() const { return m_controller; }
    void setController(QObject* controller);
    void setBackend(TagBackend* backend);           // tesztekhez; nem veszi át
    TagBackend* backend() const { return m_backend; }

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    QVariantList tags() const;
    QStringList tagIds() const { return m_tagIds; }
    QVariantList suggestions() const;
    QString suggestionLabel() const;
    QString suggestionSource() const { return m_visible.isEmpty() ? QString() : m_source; }
    bool computing() const { return m_computing; }
    bool canUndo() const { return m_backend && m_backend->canUndo(); }
    QString undoLabel() const { return m_backend ? m_backend->undoLabel() : QString(); }

    const QVector<TagSuggestionItem>& visibleSuggestions() const { return m_visible; }
    // A ténylegesen használt megbeszélés-azonosító (demóban üres helyett a demó-megbeszélés).
    QString effectiveMeetingId() const;

    // Név (vagy id) hozzáadása kézzel; visszaadja az id-t.
    Q_INVOKABLE QString add(const QString& nameOrId);
    Q_INVOKABLE void remove(const QString& tagId);
    Q_INVOKABLE void accept(int index);
    Q_INVOKABLE void reject(int index);
    Q_INVOKABLE void acceptAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void requestSuggestions();
    // A „Miért?” panel: [{ index, id, name, isNew, source,
    //   reasons: [{ kind, values, label, text }], similarMeetings: [{ meetingId, title, dateText }] }]
    Q_INVOKABLE QVariantList whyData() const;
    Q_INVOKABLE QString tagName(const QString& tagId) const;

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void tagsChanged();
    void suggestionsChanged();
    void undoChanged();
    // Értesítés a lépés után (a héj mutatja „Visszavonás”-sal).
    void toast(const QString& text, bool undoable);

private:
    void attach(TagBackend* backend);
    void applyDemoState();
    void reloadTags();
    void reloadSuggestions();
    bool applied(const TagSuggestionItem& s) const;
    void finishStep();

    QPointer<QObject> m_controller;
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QList<QMetaObject::Connection> m_conns;
    QString m_meetingId;
    QString m_demoState;

    QStringList m_tagIds;
    QStringList m_tagNames;
    QVector<TagSuggestionItem> m_visible;
    QString m_source;
    QString m_baseTagId;
    bool m_computing = false;
};

} // namespace tanara_qml
