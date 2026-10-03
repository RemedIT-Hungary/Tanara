#pragma once
//
// PersonListModel — az ismert személyek a választó panelekhez (K5: a „+" oszlop
// személyválasztója és a teljes-beszélő popover „Másik személy" listája). Gépelésre szűr
// (ékezet- és kisbetű-független, névrészletre is); üres keresőnél a gyakran szereplők
// elöl. A lista a TranscriptEditorViewModel-től jön (éles: AppController::peopleDirectory,
// demó: kitalált nevek).
//
#include "TranscriptEditorViewModel.h"

#include "tanara/edit/SpeakerEditTypes.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {


class PersonListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(tanara_qml::TranscriptEditorViewModel* editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // Ez a név kimarad (a popoverben a beszélő mostani személye).
    Q_PROPERTY(QString excludeName READ excludeName WRITE setExcludeName NOTIFY excludeNameChanged)
    // Üres keresőnél a meeting mostani résztvevői kimaradnak (nekik már van oszlopuk).
    Q_PROPERTY(bool hideMeetingPeople READ hideMeetingPeople WRITE setHideMeetingPeople NOTIFY hideMeetingPeopleChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Igaz, ha a beírt név még nem létezik → „Új személy: „…"" sor.
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, HasVoiceprintRole, MeetingCountRole, InMeetingRole };

    explicit PersonListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TranscriptEditorViewModel* editor() const { return m_editor; }
    void setEditor(TranscriptEditorViewModel* editor);
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    QString excludeName() const { return m_exclude; }
    void setExcludeName(const QString& name);
    bool hideMeetingPeople() const { return m_hideMeeting; }
    void setHideMeetingPeople(bool hide);
    int count() const { return int(m_shown.size()); }
    bool canCreate() const { return m_canCreate; }

    // A személylista újraolvasása (a panel megnyitásakor).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString nameAt(int row) const;

signals:
    void editorChanged();
    void queryChanged();
    void excludeNameChanged();
    void hideMeetingPeopleChanged();
    void countChanged();

private:
    void refilter();

    QPointer<TranscriptEditorViewModel> m_editor;
    QString m_query;
    QString m_exclude;
    bool m_hideMeeting = true;
    bool m_canCreate = false;
    QVector<tanara::PersonInfo> m_all;
    QVector<tanara::PersonInfo> m_shown;
    QVector<bool> m_inMeeting;
};

} // namespace tanara_qml
