#pragma once
//
// PersonListModel — az ismert személyek a választó panelekhez (K5: a „+" oszlop
// személyválasztója és a teljes-beszélő popover „Másik személy" listája). Gépelésre szűr
// (ékezet- és kisbetű-független, névrészletre is); üres keresőnél a gyakran szereplők
// elöl. A lista a TranscriptEditorViewModel-től jön (éles: AppController::peopleDirectory,
// demó: kitalált nevek).
//
#include "ParticipantsViewModel.h"
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
    // A „Ki volt ott?" párbeszéd forrása (az editor helyett): a személyek és a „már résztvevő".
    Q_PROPERTY(tanara_qml::ParticipantsViewModel* participants READ participants WRITE setParticipants NOTIFY participantsChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // Ez a név kimarad (a popoverben a beszélő mostani személye).
    Q_PROPERTY(QString excludeName READ excludeName WRITE setExcludeName NOTIFY excludeNameChanged)
    // Üres keresőnél a meeting mostani résztvevői kimaradnak (nekik már van oszlopuk).
    Q_PROPERTY(bool hideMeetingPeople READ hideMeetingPeople WRITE setHideMeetingPeople NOTIFY hideMeetingPeopleChanged)
    // A meeting résztvevői kereséskor sem jelennek meg (a soronkénti panelen ők külön, a
    // meeting beszélőiként szerepelnek).
    Q_PROPERTY(bool excludeMeetingPeople READ excludeMeetingPeople WRITE setExcludeMeetingPeople NOTIFY excludeMeetingPeopleChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Igaz, ha a beírt név még nem létezik → „Új személy: „…"" sor.
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY countChanged)

public:
    // MatchedAliasRole: ha a találat becenévből jött (nem a névből), az a becenév; különben üres.
    enum Role { NameRole = Qt::UserRole + 1, HasVoiceprintRole, MeetingCountRole, InMeetingRole, MatchedAliasRole };

    explicit PersonListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TranscriptEditorViewModel* editor() const { return m_editor; }
    void setEditor(TranscriptEditorViewModel* editor);
    ParticipantsViewModel* participants() const { return m_participants; }
    void setParticipants(ParticipantsViewModel* participants);
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    QString excludeName() const { return m_exclude; }
    void setExcludeName(const QString& name);
    bool hideMeetingPeople() const { return m_hideMeeting; }
    void setHideMeetingPeople(bool hide);
    bool excludeMeetingPeople() const { return m_excludeMeeting; }
    void setExcludeMeetingPeople(bool exclude);
    int count() const { return int(m_shown.size()); }
    bool canCreate() const { return m_canCreate; }

    // A személylista újraolvasása (a panel megnyitásakor).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString nameAt(int row) const;

signals:
    void editorChanged();
    void participantsChanged();
    void queryChanged();
    void excludeNameChanged();
    void hideMeetingPeopleChanged();
    void excludeMeetingPeopleChanged();
    void countChanged();

private:
    void refilter();

    QPointer<TranscriptEditorViewModel> m_editor;
    QPointer<ParticipantsViewModel> m_participants;
    QString m_query;
    QString m_exclude;
    bool m_hideMeeting = true;
    bool m_excludeMeeting = false;
    bool m_canCreate = false;
    QVector<tanara::PersonInfo> m_all;
    QVector<tanara::PersonInfo> m_shown;
    QVector<bool> m_inMeeting;
};

} // namespace tanara_qml
