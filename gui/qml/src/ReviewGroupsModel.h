#pragma once
//
// ReviewGroupsModel: the rows of the "To review" view: group cards, the rows of the expanded
// ("One by one") group with per-row actions, and the "... N rows in this group · Expand" row
// under the active collapsed group. The groups come from TranscriptEditorViewModel (the
// core's SpeakerEditor::reviewGroups() + the voice-doubtful rows outside the groups).
//
// In the expanded group an already decided row ("Looks right", reassigned) stays in place while
// the group is open, so it does not jump from under the cursor (as in the old "Uncertain"
// filter). The model does not reset on recomputation: unchanged rows stay, the difference is insert / remove.
//
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptEditorViewModel;

class ReviewGroupsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.reviewGroups adja")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        RowKindRole = Qt::UserRole + 1,     // "group" | "line" | "more"
        GroupIdRole,
        GroupKindRole,                      // ReviewView::kind
        TitleRole,
        SubtitleRole,
        CountRole,                          // the group's current row count
        EvidenceRole,                       // [evidence map] (TranscriptEditorViewModel::evidenceMap)
        ProposedKeyRole,
        ProposedNameRole,
        ActionableRole,                     // asks for a decision (not informational)
        CanApplyRole,                       // has a suggestion ("All N -> X")
        ExpandedRole,
        ActiveRole,                         // the group being worked on (accentLine frame)
        IconRole,                           // the group's icon
        UtteranceIdRole,
        SpeakerKeyRole,
        SpeakerNameRole,
        ColorIndexRole,
        TimeLabelRole,
        LineTextRole,
        StartMsRole,
        EndMsRole,
        ReasonRole,                         // "side" | "voice" | "tag" | ""
        LikelyKeyRole,                      // the row's suggested speaker (the group's suggestion)
        LikelyNameRole,
        DecidedRole,                        // the row is already decided (stays in place, without actions)
        DecidedTextRole,                    // "fixed" | "looks right"
        LaneRole,
        NewSpeakerSideRole,                 // "local" | "remote": the suggestion is a new speaker on this side
        // The decision kind for the marker: "corrected" | "confirmed" | "ignored" | "" (undecided)
        DecidedKindRole,
        SampleStateRole,                    // on a decided row "sample" | "noSample"; otherwise ""
        IgnoredRole,                        // "cannot be identified"
    };
    Q_ENUM(Role)

    explicit ReviewGroupsModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    Q_INVOKABLE void setExpanded(const QString& groupId, bool expanded);
    Q_INVOKABLE bool isExpanded(const QString& groupId) const { return m_expanded.contains(groupId); }
    Q_INVOKABLE int rowOfGroup(const QString& groupId) const;
    Q_INVOKABLE int rowOfLine(const QString& utteranceId) const;
    // The row after `fromRow` (direction < 0: before) awaiting a decision; if it is in a closed
    // group, the group opens. Wraps around. Returns the row; -1 = none.
    Q_INVOKABLE int stepLine(int fromRow, int direction = 1);
    Q_INVOKABLE QString utteranceAt(int row) const;
    Q_INVOKABLE QString groupAt(int row) const;

    // ---- called by the view model ----
    void sync();                                        // the groups changed
    void refreshUtterances(const QVector<int>& utteranceIndices);
    void refreshAll();                                  // names / colors changed
    void reset();                                       // new session: expansion, stickiness cleared

signals:
    void countChanged();

private:
    enum Kind { Group, Line, More };
    struct Row {
        Kind kind = Group;
        int view = -1;          // index a TranscriptEditorViewModel::reviewViews()-ban
        QString utteranceId;    // Line
        QString key;            // stable key for the diff update
    };
    QVector<Row> compute();
    void apply(const QVector<Row>& target);
    QString activeId() const;

    TranscriptEditorViewModel* m_vm;
    QVector<Row> m_rows;
    QSet<QString> m_expanded;
    QHash<QString, QStringList> m_shown;    // expanded group -> the displayed rows (sticky)
    QString m_active;
};

} // namespace tanara_qml
