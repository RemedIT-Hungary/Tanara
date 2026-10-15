#pragma once
//
// PersonListModel: the known people for the picker panels (the "+" column's person picker
// and the full-speaker popover's "Another person" list). Filters on typing (accent- and
// case-insensitive, name fragments too); with an empty search the frequent ones come first.
// The list comes from TranscriptEditorViewModel (live: the people directory,
// demo: fictional names).
//
// Tag ranking (UX plan, "Person picker"): already a participant -> shared tag (the person
// has a tag of the meeting manually, or the learned relation reaches the threshold; at most
// 5) -> the rest. The shared-tag row's `tagHint`: "#Nordvik · 11 / 14 · 12 meetings"; if the
// list has at least 6 rows, the group's section title is `tagSectionTitle` ("Based on
// #Nordvik"), the rows' `group` role: "meeting" | "tag" | "other".
// `tagId` mode (Tags window "+ Person"): the tag is the reference, those who have it manually
// are left out; then without editor the full name list
// (IParticipantsFacade::peopleDirectory, in the demo fictional names) is the source.
//
#include "TranscriptEditorViewModel.h"
#include "TagBackend.h"
#include "UiServices.h"

#include "tanara/editing/participants/IParticipantsFacade.h"
#include "tanara/editing/session/SpeakerEditTypes.h"

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
    // This name is left out (in the popover the speaker's current person).
    Q_PROPERTY(QString excludeName READ excludeName WRITE setExcludeName NOTIFY excludeNameChanged)
    // With an empty search the meeting's current participants are left out (they already have a column).
    Q_PROPERTY(bool hideMeetingPeople READ hideMeetingPeople WRITE setHideMeetingPeople NOTIFY hideMeetingPeopleChanged)
    // The meeting's participants do not show when searching either (in the per-row panel they
    // appear separately, as the meeting's speakers).
    Q_PROPERTY(bool excludeMeetingPeople READ excludeMeetingPeople WRITE setExcludeMeetingPeople NOTIFY excludeMeetingPeopleChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // True if the typed name does not exist yet -> a "New person: "..."" row.
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY countChanged)
    Q_PROPERTY(QString tagId READ tagId WRITE setTagId NOTIFY tagIdChanged)
    Q_PROPERTY(QString tagSectionTitle READ tagSectionTitle NOTIFY countChanged)

public:
    // MatchedAliasRole: if the hit came from a nickname (not the name), that nickname; otherwise empty.
    enum Role { NameRole = Qt::UserRole + 1, HasVoiceprintRole, MeetingCountRole, InMeetingRole, MatchedAliasRole,
                TagHintRole, GroupRole };

    // QML: the services of the moment (follows App.controller / App.demo).
    explicit PersonListModel(QObject* parent = nullptr);
    // The tag backend and the people directory of the given services.
    explicit PersonListModel(const UiServices& services, QObject* parent = nullptr);

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
    bool excludeMeetingPeople() const { return m_excludeMeeting; }
    void setExcludeMeetingPeople(bool exclude);
    int count() const { return int(m_shown.size()); }
    bool canCreate() const { return m_canCreate; }
    QString tagId() const { return m_tagId; }
    void setTagId(const QString& id);
    QString tagSectionTitle() const { return m_tagSection; }
    // For tests: the tag data source (not taken over).
    void setTagBackend(TagBackend* backend);

    // Re-reads the person list (when the panel opens).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString nameAt(int row) const;

signals:
    void editorChanged();
    void queryChanged();
    void excludeNameChanged();
    void hideMeetingPeopleChanged();
    void excludeMeetingPeopleChanged();
    void countChanged();
    void tagIdChanged();

private:
    void refilter();
    void dropServices();
    TagBackend* tagBackend();
    QStringList contextTagIds() const;

    QPointer<TranscriptEditorViewModel> m_editor;
    QString m_query;
    QString m_exclude;
    bool m_hideMeeting = true;
    bool m_excludeMeeting = false;
    bool m_canCreate = false;
    QVector<tanara::editing::PersonInfo> m_all;
    QVector<tanara::editing::PersonInfo> m_shown;
    QVector<bool> m_inMeeting;
    QStringList m_hints;
    QStringList m_groups;
    QString m_tagId;
    QString m_tagSection;
    TagBackend* m_tags = nullptr;
    bool m_ownTags = false;
    tanara::editing::IParticipantsFacade* m_directory = nullptr;   // the standalone (tag mode) name list
    bool m_explicitServices = false;
};

} // namespace tanara_qml
