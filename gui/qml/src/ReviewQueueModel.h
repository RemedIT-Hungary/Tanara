#pragma once
//
// ReviewQueueModel: the worklist of the "To review" focus-queue concept. Exposed as
// TranscriptEditorViewModel.reviewQueue; the focus view and the opening suggestion / pattern
// offer work from it.
//
// Principle: the worklist is a FROZEN, CHRONOLOGICAL snapshot taken on entry (start) or at
// round close (startNextRound) from the currently doubtful rows (rows of the ReviewViews that
// ask for a decision + every "uncertain" row; without already decided rows: confirmed,
// corrected, cannot be identified). The background analysis (rebuildReview -> syncFromReview)
// NEVER reorders it:
//  - a decided row STAYS in place (`decided` / `ignored`), the cursor moves to the next open row;
//  - new doubtful rows go into an outer "incoming" set (incomingCount, "+N new waiting") and
//    join the END of the list only on request (admitIncoming) or at round close (startNextRound);
//  - a suggestion change for a listed row is only flagged on the row (suggestionUpdated);
//  - a listed row that is no longer doubtful in the background becomes `alreadyDecided` (it does not vanish).
// Only later() (row to the end of the list) and bringToFront() ("One by one") change the order.
//
// The state derives from the row's flags (no stored decision): ignored -> `ignored`; confirmed /
// manuallyCorrected -> `decided`; Later flag -> `later`; no longer doubtful -> `alreadyDecided`;
// otherwise `pending`. So an undo restores the row by itself (and the cursor jumps to it).
//
#include "TranscriptEditorViewModel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class ReviewQueueModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.reviewQueue adja")
    // Whether a worklist is running (after start, before stop / a new session).
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The round number (from 1).
    Q_PROPERTY(int round READ round NOTIFY roundChanged)
    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    // The cursor (row index in the list; -1 = none: empty list or round end).
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentUtterance READ currentUtterance NOTIFY currentChanged)
    // The cursor row with the role names (see roleNames); empty map if none.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    // Counters for the progress bar ("N / M"): decided + ignored = resolved.
    Q_PROPERTY(int decidedCount READ decidedCount NOTIFY countsChanged)
    Q_PROPERTY(int ignoredCount READ ignoredCount NOTIFY countsChanged)
    Q_PROPERTY(int laterCount READ laterCount NOTIFY countsChanged)
    Q_PROPERTY(int alreadyDecidedCount READ alreadyDecidedCount NOTIFY countsChanged)
    // Open rows: only pending (the cursor skips self-resolved alreadyDecided rows; they are not open).
    Q_PROPERTY(int openCount READ openCount NOTIFY countsChanged)
    // "Confirm all": among self-resolved rows, those at least kMinEmbedMs long (these would be
    // samples; the button leaves the short ones alone).
    Q_PROPERTY(int confirmableCount READ confirmableCount NOTIFY countsChanged)
    // The round is done: there is a worklist and no open row (later rows are not open).
    Q_PROPERTY(bool roundComplete READ roundComplete NOTIFY countsChanged)
    // The incoming set ("+N new waiting"): doubtful rows not on the worklist; in time order.
    Q_PROPERTY(int incomingCount READ incomingCount NOTIFY incomingChanged)
    Q_PROPERTY(QStringList incomingIds READ incomingIds NOTIFY incomingChanged)

public:
    enum State { Pending, Decided, Later, Ignored, AlreadyDecided };
    Q_ENUM(State)

    enum Role {
        UtteranceIdRole = Qt::UserRole + 1,
        OrderRole,              // row index in the list (= the model row)
        StateRole,              // "pending" | "decided" | "later" | "ignored" | "alreadyDecided"
        CurrentRole,            // the cursor is on this row
        ReasonKindRole,         // the group kind: "sideConflict" | "coreMismatch" | "similarToNewPerson" | "uncertain"
        ReasonTitleRole,        // the group's title (ReviewView::title, already translated)
        ReasonsRole,            // the group's evidence (evidence maps: kind, polarity, text, value, manual ...)
        GroupIdRole,            // the ReviewView id (at admission)
        LikelyKeyRole,          // suggested speaker's key (empty: person outside the meeting / new speaker / none)
        LikelyNameRole,         // suggested speaker's name (also for a person outside the meeting)
        LikelyNewSideRole,      // the suggestion is a new speaker on this side: "local" | "remote" | ""
        SuggestionUpdatedRole,  // the suggestion changed since admission (focus shows "suggestion updated")
        SpeakerKeyRole,         // the row's CURRENT speaker
        SpeakerNameRole,
        ColorIndexRole,
        LaneRole,               // the speaker's rail column (-1 = collapsed)
        OriginalKeyRole,        // the speaker at admission (the "current" in the pattern)
        OriginalNameRole,
        StartMsRole,
        EndMsRole,
        TimeLabelRole,          // "12:58"
        LineTextRole,
        DecidedKindRole,        // "corrected" | "confirmed" | "ignored" | ""
        SampleStateRole,        // "sample" | "noSample" | ""
        AdmittedRole,           // came from the incoming set (admitIncoming / new round)
        ConsequenceRole,        // became doubtful at admission "because of your current decisions"
        RoundRole,              // the round in which it joined the list
    };
    Q_ENUM(Role)

    explicit ReviewQueueModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    int round() const { return m_round; }
    int count() const { return int(m_items.size()); }
    int currentIndex() const { return m_current; }
    QString currentUtterance() const;
    QVariantMap current() const { return get(m_current); }
    int decidedCount() const;
    int ignoredCount() const;
    int laterCount() const;
    int alreadyDecidedCount() const;
    int openCount() const;
    int confirmableCount() const { return int(confirmableIds().size()); }
    bool roundComplete() const { return m_active && openCount() == 0; }
    int incomingCount() const { return int(m_incoming.size()); }
    QStringList incomingIds() const { return m_incoming; }

    // ---- worklist ----
    // New snapshot from the currently doubtful rows (round 1); the cursor goes to the first open row.
    Q_INVOKABLE void start();
    // Exit: the list empties, the sync stops.
    Q_INVOKABLE void stop();
    // After round close: a new snapshot from the incoming and / or "later" rows (still-open rows
    // carry over too), in time order; the round counter grows, the pattern log is cleared.
    Q_INVOKABLE void startNextRound(bool includeIncoming = true, bool includeLater = true);
    // Incoming rows go to the END of the list (chronological among themselves); the cursor does
    // not move, or, if the round was done, lands on the first admitted row. Returns the number admitted.
    Q_INVOKABLE int admitIncoming();

    // ---- cursor ----
    Q_INVOKABLE bool next();            // to the next row (any state); does not wrap at the end
    Q_INVOKABLE bool previous();        // to the previous row (decided too; it can be decided again there); from -1 to the last
    Q_INVOKABLE bool advance();         // to the next OPEN row (wraps); none: -1 (round end)
    Q_INVOKABLE bool retreat();         // (Shift+B): to the previous OPEN row (wraps); none: false
    // Index of the next open row after the cursor (wraps; not the cursor's row); -1: none.
    Q_INVOKABLE int nextOpenIndex() const;
    Q_INVOKABLE bool jumpTo(int index);
    // "Later": the cursor's (pending) row goes to the END of the list with `later` state; the
    // cursor goes to the next open row. On an alreadyDecided row: on to the next open one (the row
    // stays in place); on a decided / later row: next().
    Q_INVOKABLE bool later();
    // "One by one": the given (open) rows go to the cursor's place, in time order; the cursor to the first.
    Q_INVOKABLE int bringToFront(const QStringList& utteranceIds);
    Q_INVOKABLE int indexOf(const QString& utteranceId) const { return m_pos.value(utteranceId, -1); }
    Q_INVOKABLE QVariantMap get(int index) const;
    // Self-resolved rows at least kMinEmbedMs long (in list order): "Confirm all" confirms
    // these (TranscriptEditorViewModel::confirmSettled).
    Q_INVOKABLE QStringList confirmableIds() const;

    // ---- summaries ----
    // Round close: { round, total, decided, ignored, later, alreadyDecided, confirmable, open,
    //   sampleDelta: [{ key, name, colorIndex, delta }] (since the round start, descending),
    //   incoming, incomingIds, incomingInReviewed (within the time span of the round's rows),
    //   incomingCausedByDecisions, causedIds }.
    Q_INVOKABLE QVariantMap roundSummary() const;
    // Pattern: of the last 6 decisions, the most frequent (reason, current, proposed) triple, if
    // it occurs >= 4 times: { key, reasonKind, currentKey, currentName, proposedKey, proposedName,
    //   hits, window, sameAhead: [id] (open rows with the same triple OUTSIDE the cursor's row, in
    //   list order), currentMatches (the cursor's row is such too) }. Empty map: no pattern.
    Q_INVOKABLE QVariantMap recentPattern() const;
    // "No": this triple is not offered again in this round.
    Q_INVOKABLE void dismissPattern(const QString& key);
    // Opening suggestions from the round start: groups of >= 15 rows that are strong (voice >= 75 %
    // or a track contradiction with a manual assignment) and have a suggestion: [{ groupId, kind,
    // title, subtitle, ids, count, pendingCount, currentKey, currentName, proposedKey,
    // proposedName, newSide, voicePct (-1: none), manualSide, spotIds (3 rows spread in time),
    // sampleCount (estimated sample count from the still-open rows: long enough, not noisy) }].
    Q_INVOKABLE QVariantList openers() const;

    // ---- called by the view model ----
    // start() with explicit input (test / embedding): the snapshot and opening suggestions come
    // from these groups / rows (row flags live from the VM).
    void startFrom(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                   const QVector<tanara::EditorUtterance>& utterances);
    // Result of the background analysis (after the VM's rebuildReview): only the incoming set,
    // suggestion updates and the alreadyDecided state change; never the order.
    void syncFromReview();
    // The same with explicit input (test / embedding).
    void syncFromReview(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                        const QVector<tanara::EditorUtterance>& utterances);
    // After the VM's decision calls / undo: the affected rows' state (the order does not change).
    // A decision on the cursor's row -> the cursor goes to the next open row; an undone decision
    // -> the cursor goes to the restored row.
    void refreshUtterances(const QVector<int>& utteranceIndices);
    void refreshAll();                  // names / colors / columns changed

    static QString stateName(State s);
    // The opening suggestion's thresholds (default: kOpenerMinLines / kOpenerMinVoice); demo
    // states loosen them on the small fictional meeting. The next start / new round uses them.
    void setOpenerThresholds(int minLines, double minVoice)
    {
        m_openerMinLines = minLines;
        m_openerMinVoice = minVoice;
    }

    static constexpr int kPatternWindow = 6;
    static constexpr int kPatternMinHits = 4;
    static constexpr int kOpenerMinLines = 15;
    static constexpr double kOpenerMinVoice = 0.75;

signals:
    void activeChanged();
    void roundChanged();
    void countsChanged();
    void currentChanged();
    void incomingChanged();
    void patternChanged();
    // A listed row's suggestion changed since admission (SuggestionUpdatedRole is true too).
    void suggestionUpdated(const QString& utteranceId);

private:
    struct Doubt {
        QString groupId;
        QString kind;
        QString title;
        QVariantList evidence;
        QString proposedKey;
        QString proposedName;
        QString newSide;
    };
    struct Item {
        QString id;
        QString originalKey;
        Doubt reason;               // the reason at admission (frozen)
        QString likelyKey;          // the suggestion (may refresh while the row is open)
        QString likelyName;
        QString likelyNewSide;
        bool suggestionUpdated = false;
        bool later = false;
        bool admitted = false;
        bool consequence = false;
        int round = 1;
        State state = Pending;
        QString sig;                // fingerprint of the decision flags (to detect changes)
    };
    struct Opener {
        Doubt view;
        QString currentKey;
        QStringList ids;
        int voicePct = -1;
        bool manualSide = false;
    };
    struct PatternEntry {
        QString id;
        QString key;                // empty: not a move (confirm, cannot be identified, later)
    };

    QHash<QString, Doubt> computeDoubt(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                                       const QVector<tanara::EditorUtterance>& utts) const;
    const tanara::EditorUtterance* utt(const QString& id) const;
    State stateOf(const Item& it) const;
    QString sigOf(const QString& id) const;
    Item makeItem(const QString& id, const Doubt& d, bool admitted) const;
    void setItems(QVector<Item> items);
    void rebuildPos();
    void setCurrent(int index, bool force = false);
    int nextOpen(int from) const;           // forward from `from` (exclusive), wrapping
    bool isOpen(const Item& it) const;
    void updateIncoming();
    void computeOpeners(const QVector<TranscriptEditorViewModel::ReviewView>& views);
    void beginRoundBaseline();
    QSet<QString> grownSpeakers() const;
    bool causedByDecisions(const QString& id, const QSet<QString>& grown) const;
    QString patternKey(const Item& it, const QString& targetKey) const;
    void emitRow(int row);
    void sortByTime(QStringList& ids) const;

    TranscriptEditorViewModel* m_vm;
    bool m_active = false;
    int m_round = 1;
    QVector<Item> m_items;
    QHash<QString, int> m_pos;
    int m_current = -1;
    QHash<QString, Doubt> m_doubt;          // doubtful rows of the latest sync
    QStringList m_incoming;
    QSet<QString> m_roundStartDoubt;        // rows doubtful at the round start (for marking consequences)
    QHash<QString, int> m_roundStartSamples;    // speaker -> sessionAdded at the round start
    QVector<PatternEntry> m_log;            // the round's decisions (newest last)
    QSet<QString> m_dismissed;
    QVector<Opener> m_openers;
    int m_openerMinLines = kOpenerMinLines;
    double m_openerMinVoice = kOpenerMinVoice;
};

} // namespace tanara_qml
