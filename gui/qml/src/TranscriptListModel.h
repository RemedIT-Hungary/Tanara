#pragma once
//
// TranscriptListModel: the rows of the transcript editor's ListView. A row is either an
// UTTERANCE or (in a filter: "Uncertain", the sample meter's "+N now", hiding the "cannot be
// identified" rows) a "... N rows hidden" separator.
//
// The data lives in TranscriptEditorViewModel's snapshot (utterances, speakers, selection,
// suggestion, search); this class holds the mapping of the VISIBLE rows and emits the
// fine-grained notifications. For moving a single row it NEVER resets the model: dataChanged
// goes out for the affected rows, and in a filter insert / remove for the difference.
//
#include <QAbstractListModel>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptEditorViewModel;

class TranscriptListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.rows adja")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,    // "utterance" | "gap"
        UtteranceIdRole,
        UtteranceIndexRole,
        StartMsRole,
        EndMsRole,
        TimeLabelRole,                  // "01:16" / "1:02:03"
        TextRole,
        RichTextRole,                   // search hit highlighted (HTML); empty = no hit
        SpeakerKeyRole,
        SpeakerNameRole,
        ColorIndexRole,
        LaneRole,                       // the lane's column index; -1 = in the collapsed "+N" group
        HeadRole,                       // speaker change -> the name shows
        FirstRole,                      // the list's first row (no turn spacing above it)
        UncertainRole,
        CorrectedRole,
        SelectedRole,
        SuggestedRole,                  // part of the "similar lines" suggestion, and currently shown
        SuggestionAnchorRole,           // the row that triggered the suggestion (manually fixed)
        HiddenCountRole,                // separator: this many sure rows are hidden
        NoisyRole,                      // "overlapping speech": not a voice sample (automatic or manual)
        NoisyOverlapRole,               // noisy by the overlap rule (independent of the manual override)
        LikelySpeakerKeyRole,           // the re-check's suggestion: the speaker that fits the voice better
        LikelySpeakerNameRole,
        // Row markers
        UncertainReasonRole,            // "side" | "voice" | "tag" | "" -- the reason for the "uncertain" pill
        ConfirmedRole,                  // "Looks right" (check mark)
        ShortRole,                      // shorter than what the audio analysis judges reliably ("short")
        SideConflictRole,               // the row spoke on the track opposite to its speaker's side
        NewPersonRole,                  // the row's speaker is the person just created ("new person")
        SimilarRole,                    // a row similar to the new person, highlighted (rail: frame in its column)
        // S2
        IgnoredRole,                    // "cannot be identified": the name stays, the audio analysis skips it
        SampleStateRole,                // on a decided row "sample" | "noSample"; otherwise ""
        // S6
        SweepHandledRole,               // sweep: the row is already decided / skipped / "Later" (not a stop)
    };
    Q_ENUM(Role)

    struct Row {
        bool gap = false;
        int utterance = -1;     // utterance index; for a separator the first index of the hidden run
        int hidden = 0;         // separator: the number of hidden rows
        int key() const { return gap ? 2 * utterance : 2 * utterance + 1; }
    };

    explicit TranscriptListModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    const QVector<Row>& rows() const { return m_rows; }
    // The utterance's row in the list; -1 if currently not visible (filter).
    int rowOfUtterance(int utteranceIndex) const;
    // The row's utterance index; -1 for a separator / invalid row.
    int utteranceOfRow(int row) const;
    // The visible utterance row nearest to the utterance (gives a hit in a filter too).
    int nearestRow(int utteranceIndex) const;

    // ---- called by the view model ----
    void rebuild();                                     // full rebuild (model reset)
    void syncFilter();                                  // refreshes the filter rows by difference
    void notifyUtterances(const QVector<int>& utteranceIndices, const QList<int>& roles = {});
    void notifyAll(const QList<int>& roles);

signals:
    void countChanged();

private:
    QVector<Row> computeRows() const;
    bool headAt(int row) const;

    TranscriptEditorViewModel* m_vm;
    QVector<Row> m_rows;
    QVector<int> m_rowOfUtt;    // utterance index -> row (-1 = hidden)
    void reindex();
};

} // namespace tanara_qml
