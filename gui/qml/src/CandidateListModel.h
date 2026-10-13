#pragma once
//
// CandidateListModel: candidates with evidence ("SUGGESTED" for a row, "NOT THEM? ACTUALLY..."
// for a speaker). For a row (utteranceId) the ranking of SpeakerEditor::lineCandidates, for a
// whole speaker (speakerKey, with an empty utteranceId) speakerCandidates. In per-row mode the
// current speaker is at the end of the list ("is them now", dim, with the evidence against);
// a candidate speaking on the other track is dim but selectable. A candidate excluded by the
// manual track assignment (Candidate::vetoed) is at the very BOTTOM, dim, with a "not them per
// their track" chip; still selectable by hand, gets no shortcut key. It refreshes by itself at
// the end of the background analysis.
//
//   CandidateListModel { editor: editorVm; utteranceId: "u12"; limit: 3; query: search.text }
//
#include "TranscriptEditorViewModel.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class CandidateListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(tanara_qml::TranscriptEditorViewModel* editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString utteranceId READ utteranceId WRITE setUtteranceId NOTIFY queryChanged)
    Q_PROPERTY(QString speakerKey READ speakerKey WRITE setSpeakerKey NOTIFY queryChanged)
    // Filter by name (accent- and case-insensitive). With a non-empty search there is no count limit.
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // At most this many candidates (besides the current speaker); 0 = all.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SpeakerKeyRole = Qt::UserRole + 1,  // empty: a known person outside the meeting
        NameRole,
        PersonNameRole,
        ColorIndexRole,                     // -1: a person outside the meeting (neutral avatar)
        SubTextRole,                        // "86 rows here" / "is them now" / "12 meetings"
        EvidenceRole,                       // [evidence map]
        OtherSideRole,
        CurrentRole,                        // the row's current speaker
        DimmedRole,                         // dim (another track / excluded / current speaker)
        KeyHintRole,                        // "1".."3" (shortcut key) or ""
        ScoreRole,
        VetoedRole,                         // excluded by the manual track assignment: at the bottom of the list
    };
    Q_ENUM(Role)

    explicit CandidateListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TranscriptEditorViewModel* editor() const { return m_editor; }
    void setEditor(TranscriptEditorViewModel* editor);
    QString utteranceId() const { return m_utteranceId; }
    void setUtteranceId(const QString& id);
    QString speakerKey() const { return m_speakerKey; }
    void setSpeakerKey(const QString& key);
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int count() const { return int(m_items.size()); }

    // The row's data (QML: choosing by key / Enter).
    Q_INVOKABLE QVariantMap get(int row) const;
    // The row for the given `keyHint` (1-3); -1 = none.
    Q_INVOKABLE int rowForKey(int key) const;
    Q_INVOKABLE void refresh();

signals:
    void editorChanged();
    void queryChanged();
    void countChanged();

private:
    struct Item {
        QString speakerKey;
        QString name;
        QString personName;
        int colorIndex = -1;
        QString subText;
        QVariantList evidence;
        bool otherSide = false;
        bool vetoed = false;
        bool current = false;
        QString keyHint;
        double score = 0.0;
    };
    void scheduleRefresh();

    QPointer<TranscriptEditorViewModel> m_editor;
    QString m_utteranceId;
    QString m_speakerKey;
    QString m_query;
    int m_limit = 0;
    QVector<Item> m_items;
    bool m_refreshQueued = false;
};

} // namespace tanara_qml
