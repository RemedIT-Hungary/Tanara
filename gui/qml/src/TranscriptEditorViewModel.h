#pragma once
//
// TranscriptEditorViewModel: view model of the "Transcript" tab (transcript editor): the
// editing session of ONE open meeting, exposed to QML. It builds on the core editing session
// (tanara::editing::SpeakerEditSession: the actions, undo, suggestions, "To review" analysis) and
// its RecheckService (the audio analysis and the re-checks); this class adds the display state:
// list model, tracks (collapse rule), overview, selection, "Uncertain" filter, search, playback
// following, suggestion, audio-analysis state.
//
// Where the session comes from: the transcript editor's facade (tanara::editing::ITranscriptEditorFacade)
// opens it for the meetingId:
//  - from QML: the App singleton's services (AppContext::services().transcriptEditor): the real one
//    (the application's editor sessions), or with App.demo / without a controller the FICTIONAL
//    meeting of tanara_demo (TranscriptEditorDemoFacade, one per demoVariant)
//  - from C++: a given facade (constructor)
//  - tests: setSession() + setPeopleProvider() directly
//
#include "TranscriptListModel.h"

#include "tanara/editing/ITranscriptEditorFacade.h"
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/library/tags/PersonTags.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace tanara_qml {

class ReviewQueueModel;     // ReviewQueueModel.h includes this header

class TranscriptEditorViewModel : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    Q_MOC_INCLUDE("ReviewQueueModel.h")

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoVariant READ demoVariant WRITE setDemoVariant NOTIFY demoVariantChanged)
    // True: the session is the fictional meeting of a demo facade.
    Q_PROPERTY(bool demo READ demo NOTIFY sessionChanged)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY sessionChanged)
    // The meeting HAS a transcript but in the old format (no utterance list), so it cannot be
    // edited here; it appears after a re-transcription.
    Q_PROPERTY(bool legacyTranscript READ legacyTranscript NOTIFY sessionChanged)
    // For an old-format transcript, the text of transcript.md (read-only; empty if no file).
    Q_PROPERTY(QString legacyText READ legacyText NOTIFY sessionChanged)
    Q_PROPERTY(tanara_qml::TranscriptListModel* rows READ rows CONSTANT)

    Q_PROPERTY(int utteranceCount READ utteranceCount NOTIFY sessionChanged)
    Q_PROPERTY(int speakerCount READ speakerCount NOTIFY speakersChanged)
    // The visible lanes: [{ key, name, personName, colorIndex, hasVoiceprint, anonymous,
    // added, isSelf, utteranceCount, pct, voiceprint }], myself first, then by first appearance.
    // `voiceprint`: "has" | "none" (named but no voiceprint) | "anonymous".
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY speakersChanged)
    // All speakers (collapsed ones too), same elements + `lane` (-1 = collapsed).
    Q_PROPERTY(QVariantList speakers READ speakers NOTIFY speakersChanged)
    Q_PROPERTY(int collapsedCount READ collapsedCount NOTIFY speakersChanged)
    Q_PROPERTY(bool lanesExpanded READ lanesExpanded WRITE setLanesExpanded NOTIFY speakersChanged)
    // Overview: [{ key, name, colorIndex (-1 = "Other"), pct, voiceprint ("has" | "none" |
    // "anonymous"; empty for "Other"), segments: [x, w, ...], marks: [x, w, ...] }]
    Q_PROPERTY(QVariantList overview READ overview NOTIFY overviewChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY overviewChanged)
    // The player's length (if longer than the last utterance, the overview adapts to it).
    Q_PROPERTY(int timelineMs READ timelineMs WRITE setTimelineMs NOTIFY overviewChanged)

    Q_PROPERTY(bool railVisible READ railVisible WRITE setRailVisible NOTIFY railVisibleChanged)
    Q_PROPERTY(bool uncertainOnly READ uncertainOnly WRITE setUncertainOnly NOTIFY uncertainOnlyChanged)
    Q_PROPERTY(int uncertainCount READ uncertainCount NOTIFY uncertainCountChanged)
    // Re-check from confirmed lines: whether it can run now, and if not, why
    // (a sentence; empty if it can run).
    Q_PROPERTY(bool canRecheck READ canRecheck NOTIFY recheckStateChanged)
    Q_PROPERTY(QString recheckBlocker READ recheckBlocker NOTIFY recheckStateChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    // The single selected line (or the selection's "anchor") in the list; -1 = none.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY selectionChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)

    Q_PROPERTY(bool suggestionActive READ suggestionActive NOTIFY suggestionChanged)
    Q_PROPERTY(int suggestionCount READ suggestionCount NOTIFY suggestionChanged)
    Q_PROPERTY(QString suggestionTargetName READ suggestionTargetName NOTIFY suggestionChanged)
    // Pairwise review offer on the lane: after a manual reassignment the "similar lines"
    // suggestion stayed silent because the two voices are similar, but both named speakers have
    // at least 3 confirmed lines -> "A and B sound alike. Review both of their lines...?"
    Q_PROPERTY(bool pairOfferActive READ pairOfferActive NOTIFY suggestionChanged)
    Q_PROPERTY(QString pairOfferText READ pairOfferText NOTIFY suggestionChanged)
    // "Show me": the suggested lines are highlighted on the rail and the overview.
    Q_PROPERTY(bool suggestionShown READ suggestionShown WRITE setSuggestionShown NOTIFY suggestionChanged)

    // Notice of the latest reassignment (bottom bar): what happened and what can be continued.
    // Each new reassignment replaces it; undo / redo / other edits clear it.
    Q_PROPERTY(bool changeActive READ changeActive NOTIFY changeChanged)
    Q_PROPERTY(int changeSerial READ changeSerial NOTIFY changeChanged)     // grows per lane (timer)
    Q_PROPERTY(QString changeText READ changeText NOTIFY changeChanged)
    // The line moved last in the list (should stay visible); -1 = none.
    Q_PROPERTY(int changeRow READ changeRow NOTIFY changeChanged)
    // "All N lines of <source>": the source speaker's remaining lines (0 = no such continuation).
    Q_PROPERTY(int changeRestCount READ changeRestCount NOTIFY changeChanged)
    Q_PROPERTY(QString changeRestText READ changeRestText NOTIFY changeChanged)
    Q_PROPERTY(QString changeSourceKey READ changeSourceKey NOTIFY changeChanged)
    Q_PROPERTY(QString changeTargetKey READ changeTargetKey NOTIFY changeChanged)
    // Voiceprint offer on the lane: a COMPLETE speaker just got a name, the person has no
    // voiceprint yet, and there is enough material for one. Never true after a per-line move.
    Q_PROPERTY(bool changeVoiceprintOffer READ changeVoiceprintOffer NOTIFY changeChanged)
    // A voiceprint was just made from the lane: the lane's "Undo" deletes this voiceprint.
    Q_PROPERTY(bool changeVoiceprintCreated READ changeVoiceprintCreated NOTIFY changeChanged)
    // False: the lane only informs now (after undoing the voiceprint just made).
    Q_PROPERTY(bool changeUndoable READ changeUndoable NOTIFY changeChanged)

    // Audio analysis (utterance embeddings): the basis of uncertainty and suggestions.
    Q_PROPERTY(bool voiceAvailable READ voiceAvailable NOTIFY voiceStateChanged)
    Q_PROPERTY(bool embeddingRunning READ embeddingRunning NOTIFY voiceStateChanged)
    Q_PROPERTY(qreal embeddingProgress READ embeddingProgress NOTIFY voiceStateChanged)
    // Non-empty: why there is no audio analysis (to show once, in a subdued way).
    Q_PROPERTY(QString voiceNote READ voiceNote NOTIFY voiceStateChanged)

    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)
    Q_PROPERTY(int searchMatchCount READ searchMatchCount NOTIFY searchChanged)
    Q_PROPERTY(int searchCurrent READ searchCurrent NOTIFY searchChanged)   // 0-based; -1 = none
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor highlightCurrentColor READ highlightCurrentColor WRITE setHighlightCurrentColor NOTIFY highlightColorChanged)

    // Row of the utterance currently playing; -1 = none (or hidden by the filter).
    Q_PROPERTY(int playingRow READ playingRow NOTIFY playingRowChanged)

    // ---- Fix mode: review groups, new person ----
    // The "To review" view appears on the `uncertainOnly` switch (the "To review N" chip).
    // Number of lines to review: lines of the groups asking for a decision (without the short
    // lines' informational group and the contaminated core): the toolbar chip.
    Q_PROPERTY(int reviewCount READ reviewCount NOTIFY reviewChanged)
    // Number of groups asking for a decision ("84 / 1298 lines, 3 groups").
    Q_PROPERTY(int reviewGroupCount READ reviewGroupCount NOTIFY reviewChanged)
    // The background analysis (side analysis, candidates, groups) is running.
    Q_PROPERTY(bool reviewRunning READ reviewRunning NOTIFY reviewChanged)
    // Contaminated core (the banner): { groupId, speakerKey, name, title, count, confirmed };
    // empty map if none.
    Q_PROPERTY(QVariantMap contaminated READ contaminated NOTIFY reviewChanged)
    // Column of the person just created (in this session); empty = none.
    Q_PROPERTY(QString newPersonKey READ newPersonKey NOTIFY reviewChanged)
    // Change bar "new person" row: true if the latest reassignment created a new person.
    Q_PROPERTY(bool changeNewPerson READ changeNewPerson NOTIFY changeChanged)
    // "N more lines sound similar...": { groupId, count, name, lines (the new person's lines),
    // detail }; empty if there are no similar lines.
    Q_PROPERTY(QVariantMap newPersonSimilar READ newPersonSimilar NOTIFY reviewChanged)
    // New person on a tagged meeting: the change bar's question
    // { text, pairs: [{ index, name, tagName, text }], answer }, e.g. "Alex Smith is a new
    // person. Add the #Nordvik tag?"; after the answer, answer holds the feedback (with Undo).
    // Empty map: no question. Asks only once (TagService::markLearningAsked).
    Q_PROPERTY(QVariantMap changeTagQuestion READ changeTagQuestion NOTIFY changeChanged)
    // Voiceprint readiness for the new person: { text, ready, pending }; empty if not meaningful.
    Q_PROPERTY(QVariantMap newPersonReadiness READ newPersonReadiness NOTIFY reviewChanged)
    // Highlight of lines similar to the new person (rail: 2 px accent frame, map: accent mark).
    Q_PROPERTY(bool similarShown READ similarShown WRITE setSimilarShown NOTIFY reviewChanged)
    // The highlighted similar lines for the map: [x, w, x, w, ...] (0..1, on the timeline). The
    // same ones are in the `overview` rows' `marks` list too (the map dock's accent marks).
    Q_PROPERTY(QVariantList similarMarks READ similarMarks NOTIFY reviewChanged)

    // ---- shared base of the "To review" redesign ----
    // The "Just now" row: the last decision { text, time ("12:58"; empty if no row), utteranceId,
    // chips: [{ kind: "sample" | "noSample" | "ignored" | "unignored" | "changed", text }], undone
    // (true: feedback of an undo), verb (for the icon: "move": line(s) went to another speaker,
    // "keep": the name stayed (Looks right / confirm), "ignore": cannot be identified, "":
    // other) }; empty map: no decision yet.
    Q_PROPERTY(QVariantMap lastDecision READ lastDecision NOTIFY decisionsChanged)
    // The previous decisions (at most 5, newest first), with the elements of lastDecision.
    Q_PROPERTY(QVariantList decisionLog READ decisionLog NOTIFY decisionsChanged)
    // Deviating sample (SpeakerEditSession::lastSampleDeviation, only if deviates): { utteranceId,
    // speakerKey, name, pct (0..100), similarity, text: "This sample differs from X's other
    // lines (voice 41%). Sure?" }; empty map: no warning.
    Q_PROPERTY(QVariantMap sampleDeviation READ sampleDeviation NOTIFY sampleDeviationChanged)
    // Sample meter (SpeakerEditSession::sampleStats): [{ key, name, colorIndex, sampleLines, sampleMs,
    // sessionAdded, sessionAddedIds, durationText ("1 min 12 s"), text ("23 samples, +4 now") }].
    Q_PROPERTY(QVariantList sampleStats READ sampleStats NOTIFY sampleStatsChanged)
    // Number of "Cannot be identified" lines; the Fix list can hide them (hideIgnored).
    Q_PROPERTY(int ignoredCount READ ignoredCount NOTIFY ignoredCountChanged)
    Q_PROPERTY(bool hideIgnored READ hideIgnored WRITE setHideIgnored NOTIFY listFilterChanged)
    // The sample meter's "+N now" filter: only the samples this speaker got in this session
    // show in the list (empty = no filter).
    Q_PROPERTY(QString sampleFilterKey READ sampleFilterKey NOTIFY listFilterChanged)
    // The list's currently active filter: "" | "uncertain" | "samples" | "hideIgnored".
    Q_PROPERTY(QString listFilter READ listFilter NOTIFY listFilterChanged)

    // ---- the "To review" worklist (focus queue; ReviewQueueModel.h) ----
    // Frozen, chronological worklist with a cursor, an incoming set and round closing.
    Q_PROPERTY(tanara_qml::ReviewQueueModel* reviewQueue READ reviewQueue CONSTANT)
    // Whether the worklist is running (after reviewQueue.start(), before stop() / a new session).
    Q_PROPERTY(bool queueActive READ queueActive NOTIFY queueActiveChanged)
    // ---- sweep mode (in the Fix view: fix while listening) ----
    // When on, playback stops at doubtful lines (stop mode) or, in continuous mode, keeps going
    // and the doubtful line stays the key target for 3 s more (the "after window"). The
    // visible rows (+/- 2) are frozen: marker changes from the background analysis stay pending there.
    Q_PROPERTY(bool sweepActive READ sweepActive WRITE setSweepActive NOTIFY sweepChanged)
    Q_PROPERTY(bool sweepContinuous READ sweepContinuous WRITE setSweepContinuous NOTIFY sweepChanged)
    // The stop row (playback stopped here, the decision bar is below); -1 = no stop.
    Q_PROPERTY(int sweepStopRow READ sweepStopRow NOTIFY sweepChanged)
    // Continuous mode: the after-window row (the doubtful line just played); -1 = none.
    Q_PROPERTY(int sweepAfterRow READ sweepAfterRow NOTIFY sweepChanged)
    // The sweep keys' target row: stop > after window > the playing row; -1 = none.
    Q_PROPERTY(int sweepTargetRow READ sweepTargetRow NOTIFY sweepChanged)
    // The frozen window (in rows; visible area +/- 2 rows): first / last row, -1 = none.
    Q_PROPERTY(int sweepFrozenFirstRow READ sweepFrozenFirstRow NOTIFY sweepWindowChanged)
    Q_PROPERTY(int sweepFrozenLastRow READ sweepFrozenLastRow NOTIFY sweepWindowChanged)
    // The frozen window's time for the lane ("14:07", "15:27").
    Q_PROPERTY(QString sweepFrozenFrom READ sweepFrozenFrom NOTIFY sweepWindowChanged)
    Q_PROPERTY(QString sweepFrozenTo READ sweepFrozenTo NOTIFY sweepWindowChanged)
    // Rows (utterance ids) of marker changes held back in the frozen window.
    Q_PROPERTY(QStringList pendingMarkerIds READ pendingMarkerIds NOTIFY sweepMarkersChanged)
    // "N lines doubtful again": lines already passed that the background analysis marked doubtful.
    Q_PROPERTY(int rebehindCount READ rebehindCount NOTIFY sweepMarkersChanged)
    // The same for the list: [{ utteranceId, row, time, name, colorIndex }] in time order.
    Q_PROPERTY(QVariantList rebehindRows READ rebehindRows NOTIFY sweepMarkersChanged)
    // Number of doubtful lines ahead of you (not yet reached).
    Q_PROPERTY(int sweepAheadCount READ sweepAheadCount NOTIFY sweepMarkersChanged)
    // Sections (12 minutes or 20 doubtful lines, whichever comes first): [{ index, startMs, endMs,
    // start, end (0..1), label ("12:00-24:00"), uncertain, done, active }].
    Q_PROPERTY(QVariantList sweepSections READ sweepSections NOTIFY sweepSectionsChanged)
    Q_PROPERTY(int sweepSectionIndex READ sweepSectionIndex NOTIFY sweepSectionsChanged)
    // The summary at the end of a section: { index, label, decided, ignored, later, background,
    // samplesText, rebehind, nextLabel, last }; empty map: no summary.
    Q_PROPERTY(QVariantMap sweepSectionSummary READ sweepSectionSummary NOTIFY sweepSectionsChanged)
    // The dock's sweep layer: { uncertain: [x...], decided: [x...], frozenStart, frozenEnd, sections:
    // [{ start, end, done, active }], doneCount, total, aheadCount } (0..1); empty map: no sweep.
    Q_PROPERTY(QVariantMap sweepDock READ sweepDock NOTIFY sweepDockChanged)
    // Auto-play of the "To review" focus view (the facade's switch: settings.json reviewAutoPlay; the
    // demo facade and a test without a facade keep it in memory). On by default.
    Q_PROPERTY(bool reviewAutoPlay READ reviewAutoPlay WRITE setReviewAutoPlay NOTIFY reviewAutoPlayChanged)

public:
    struct SpeakerView {
        QString name;
        int colorIndex = 0;
        int lane = -1;
    };

    // From QML: the facade comes from the App singleton's services (AppContext::services()).
    explicit TranscriptEditorViewModel(QObject* parent = nullptr);
    // With a given facade (hosts, tests); it is not taken over.
    explicit TranscriptEditorViewModel(tanara::editing::ITranscriptEditorFacade* facade, QObject* parent = nullptr);
    ~TranscriptEditorViewModel() override;

    // When instantiated from QML, the session resolves AFTER the properties are set.
    void classBegin() override { m_deferred = true; }
    void componentComplete() override;
    // From C++ (tests): opens the session of the current meetingId (demoVariant: the fictional one's variant).
    void resolveSession();

    // ---- session -------------------------------------------------------
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoVariant() const { return m_demoVariant; }
    void setDemoVariant(const QString& variant);
    bool demo() const { return m_facade && !m_facade->live(); }
    bool hasTranscript() const { return !m_utts.isEmpty(); }
    bool legacyTranscript() const { return m_legacyTranscript && m_utts.isEmpty(); }
    QString legacyText() const { return legacyTranscript() ? m_legacyText : QString(); }
    TranscriptListModel* rows() const { return m_rows; }

    // For tests / embedding: sets the session (and its recheck service) and the person list
    // directly. The caller keeps ownership. nullptr -> empty session.
    void setSession(tanara::editing::SpeakerEditSession* session, tanara::editing::RecheckService* recheck);
    tanara::editing::SpeakerEditSession* session() const { return m_session; }
    tanara::editing::RecheckService* recheck() const { return m_recheck; }
    tanara::editing::ITranscriptEditorFacade* facade() const { return m_facade; }
    void setPeopleProvider(std::function<QVector<tanara::editing::PersonInfo>()> provider);
    // Lane visibility (per meeting) is stored in this JSON file instead of the facade's view state
    // (tests); empty -> in memory only.
    void setUiStatePath(const QString& path);

    // ---- read state ---------------------------------------------------
    int utteranceCount() const { return int(m_utts.size()); }
    int speakerCount() const { return int(m_speakers.size()); }
    QVariantList lanes() const { return m_lanes; }
    QVariantList speakers() const { return m_speakerList; }
    int collapsedCount() const { return m_collapsedShown; }
    bool lanesExpanded() const { return m_lanesExpanded; }
    void setLanesExpanded(bool expanded);
    QVariantList overview() const { return m_overview; }
    int durationMs() const { return int(m_durationMs); }
    int timelineMs() const { return int(m_timelineMs); }
    void setTimelineMs(int ms);

    bool railVisible() const { return m_railVisible; }
    void setRailVisible(bool visible);
    bool uncertainOnly() const { return m_uncertainOnly; }
    void setUncertainOnly(bool on);
    int uncertainCount() const { return m_uncertainCount; }
    bool canRecheck() const { return m_canRecheck; }
    QString recheckBlocker() const { return m_recheckBlocker; }
    int selectedCount() const { return int(m_selected.size()); }
    int currentRow() const;

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;

    bool suggestionActive() const { return m_suggestionAnchor >= 0; }
    int suggestionCount() const { return int(m_suggested.size()); }
    QString suggestionTargetName() const { return m_suggestionTargetName; }
    bool suggestionShown() const { return m_suggestionShown; }
    void setSuggestionShown(bool shown);
    bool pairOfferActive() const { return m_pairOffer.isValid(); }
    QString pairOfferText() const;

    bool changeActive() const { return m_change.active; }
    int changeSerial() const { return m_changeSerial; }
    QString changeText() const { return m_change.text; }
    int changeRow() const;
    int changeRestCount() const { return m_change.restCount; }
    QString changeRestText() const { return m_change.restText; }
    QString changeSourceKey() const { return m_change.restCount > 0 ? m_change.sourceKey : QString(); }
    QString changeTargetKey() const { return m_change.targetKey; }
    bool changeVoiceprintOffer() const { return m_change.active && m_change.voiceprint == Change::Offer; }
    bool changeVoiceprintCreated() const { return m_change.active && m_change.voiceprint == Change::Created; }
    bool changeUndoable() const { return m_change.active && m_change.voiceprint != Change::Removed; }

    bool voiceAvailable() const;
    bool embeddingRunning() const;
    qreal embeddingProgress() const { return m_embeddingProgress; }
    QString voiceNote() const { return m_voiceNote; }

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);
    int searchMatchCount() const { return int(m_matches.size()); }
    int searchCurrent() const { return m_searchCurrent; }
    QColor highlightColor() const { return m_highlight; }
    void setHighlightColor(const QColor& c);
    QColor highlightCurrentColor() const { return m_highlightCurrent; }
    void setHighlightCurrentColor(const QColor& c);

    int playingRow() const { return m_playingRow; }

    int reviewCount() const { return m_reviewCount; }
    int reviewGroupCount() const { return m_reviewGroupCount; }
    bool reviewRunning() const;
    QVariantMap contaminated() const { return m_contaminated; }
    QString newPersonKey() const { return m_newPersonKey; }
    bool changeNewPerson() const { return m_change.active && m_change.newPerson; }
    QVariantMap changeTagQuestion() const;
    QVariantMap newPersonSimilar() const;
    QVariantMap newPersonReadiness() const;
    bool similarShown() const { return m_similarShown; }
    void setSimilarShown(bool shown);
    QVariantList similarMarks() const { return m_similarMarks; }

    QVariantMap lastDecision() const;
    QVariantList decisionLog() const;
    QVariantMap sampleDeviation() const { return m_sampleDeviation; }
    QVariantList sampleStats() const { return m_sampleStats; }
    int ignoredCount() const { return m_ignoredCount; }
    bool hideIgnored() const { return m_hideIgnored; }
    void setHideIgnored(bool hide);
    QString sampleFilterKey() const { return m_sampleFilterKey; }
    QString listFilter() const;
    // Read by the list model: the visible rows of the filter (the sample filter's ids).
    bool inSampleFilter(int utterance) const { return m_sampleFilter.contains(utterance); }
    // The row's sample state for the marker: "sample" | "noSample" | "" (undecided / not identifiable).
    QString sampleState(int utterance) const;

    // ---- evidence / candidates (read by CandidateListModel and the panels) ----
    QVector<tanara::Candidate> candidatesForLine(const QString& utteranceId) const;
    QVector<tanara::Candidate> candidatesForSpeaker(const QString& speakerKey) const;
    // One piece of evidence for QML: { kind (EvidenceChip), polarity, side ("mic" | "loopback" | ""),
    // text (chip), label ("Voice:"), sentence (the "Why...?" blocks' sentence), detail, fixTarget,
    // fixLabel, value }. sideContext: the side of the "supporting" track evidence (the line's / speaker's side).
    QVariantMap evidenceMap(const tanara::Evidence& e, const QString& sideContext = QString(),
                            bool forLine = false) const;
    QVariantList evidenceList(const QVector<tanara::Evidence>& list, const QString& sideContext = QString(),
                              bool forLine = false, bool chipsOnly = false) const;
    // The speaker's side per the track analysis: "local" | "remote" | "mixed" | "unknown".
    QString speakerSideName(const QString& speakerKey) const;
    // The row's side: "local" | "remote" | "mixed" | "unknown".
    QString lineSideName(const QString& utteranceId) const;
    // Read by the group model: the utterance index for the id (-1 = none).
    int utteranceIndexOf(const QString& utteranceId) const { return m_uttIndex.value(utteranceId, -1); }
    struct ReviewView {
        QString id;
        QString kind;           // "sideConflict" | "coreMismatch" | "similarToNewPerson" | "shortLines" |
                                // "uncertain" (voice-doubtful lines outside the groups; built by the VM)
        QString title;
        QString subtitle;
        QStringList ids;
        QString currentKey;
        QString proposedKey;
        QString proposedPerson; // PersonId string of the proposed person (empty = none)
        QString proposedName;   // its display name
        QString newSide;        // G10: the suggestion is a new speaker on this side ("local" | "remote"; empty = none)
        QVariantList evidence;
        bool actionable = true; // asks for a decision (short lines only inform)
    };
    const QVector<ReviewView>& reviewViews() const { return m_reviewViews; }
    QString displayName(const QString& key) const { return m_views.value(key).name; }

    // ---- read by the list model -------------------------------------------
    const QVector<tanara::editing::EditorUtterance>& utterances() const { return m_utts; }
    SpeakerView speakerView(const QString& key) const { return m_views.value(key); }
    bool isSelected(int utterance) const { return m_selected.contains(utterance); }
    bool isSuggested(int utterance) const { return m_suggestionShown && m_suggested.contains(utterance); }
    // A line similar to the new person that is being shown (rail: frame in the new person's column).
    bool isSimilar(int utterance) const { return m_similarShown && m_similar.contains(utterance); }
    bool isShort(int utterance) const;
    bool isNewPerson(const QString& speakerKey) const { return !m_newPersonKey.isEmpty() && speakerKey == m_newPersonKey; }
    bool isSideConflict(const QString& utteranceId) const { return m_sideConflicts.contains(utteranceId); }
    // A line just fixed in the "Uncertain" filter: stays visible until the filter is applied again
    // (it must not vanish under the cursor, and no other line may slide under the next click).
    bool isSticky(int utterance) const { return m_sticky.contains(utterance); }
    int suggestionAnchor() const { return m_suggestionAnchor; }
    QString richText(int utterance) const;
    static QString timeLabel(qint64 ms);

    // ---- position / playback ---------------------------------------------
    // The player's position; active = there is something to highlight (playing, or stopped on a row).
    Q_INVOKABLE void setPlaybackPosition(int ms, bool active);
    Q_INVOKABLE int rowForTime(int ms) const;               // the nearest VISIBLE row
    Q_INVOKABLE int utteranceForTime(int ms) const;
    Q_INVOKABLE qreal rowStartFraction(int row) const;      // 0..1 on the timeline
    Q_INVOKABLE qreal rowEndFraction(int row) const;
    Q_INVOKABLE int timeAtFraction(qreal fraction) const;
    Q_INVOKABLE int rowStartMs(int row) const;
    // The row's utterance id (right-click menu "sample from this line"); empty if not a row.
    Q_INVOKABLE QString rowUtteranceId(int row) const;
    Q_INVOKABLE int rowEndMs(int row) const;
    // The row's utterance for the per-line panel: { utteranceId, speakerKey, timeLabel, startMs,
    // endMs }; empty for a separator / invalid row.
    Q_INVOKABLE QVariantMap rowInfo(int row) const;
    // Neighbor bounds for playing a line (all utterances, regardless of the filter):
    // { minStartMs: latest end of the lines starting before it (at most startMs), maxEndMs: earliest
    // start of the lines starting after it (at least endMs) }; -1 = no such neighbor.
    Q_INVOKABLE QVariantMap playBounds(int startMs, int endMs) const;

    // ---- selection --------------------------------------------------------
    // Click on a row: plain -> only this one (clicking again clears it); toggle (Ctrl) ->
    // add / remove; range (Shift) -> from the anchor to here.
    Q_INVOKABLE void selectRow(int row, bool toggle = false, bool range = false);
    Q_INVOKABLE void selectOnly(int row);
    Q_INVOKABLE void selectRows(int fromRow, int toRow);
    Q_INVOKABLE void clearSelection();
    // Up / down: move the selection; returns the new row (-1 = did not move).
    Q_INVOKABLE int stepSelection(int delta);
    // Previous / next row (map dock buttons, arrows): while playing, steps from the row under the
    // player, otherwise from the selected (current) row; selects the target row.
    // { row, startMs }; row = -1 if there is nowhere to go.
    Q_INVOKABLE QVariantMap stepLine(int delta, bool playing);
    Q_INVOKABLE bool isRowSelected(int row) const;

    // ---- actions (each one undo step) --------------------------------------
    // Click into another column of the row. If the row is part of a multi-row selection, the
    // whole selection moves.
    // noisy (Shift = "not a sample"): the row also gets an "overlapping speech" flag, in the SAME
    // undo step (SpeakerEditSession::moveUtterances noisy); on every move.
    Q_INVOKABLE bool moveRowToLane(int row, int lane, bool noisy = false);
    // Drag: rows [fromRow, toRow] to the speaker of column `lane`.
    Q_INVOKABLE bool moveRowsToLane(int fromRow, int toRow, int lane, bool noisy = false);
    Q_INVOKABLE bool moveSelectionToLane(int lane, bool noisy = false);     // keys 1-9, chip
    Q_INVOKABLE bool moveSelectionToSpeaker(const QString& speakerKey, bool noisy = false);
    Q_INVOKABLE bool moveSelectionToPerson(const QString& personName, bool noisy = false);
    Q_INVOKABLE bool moveSelectionToNewParticipant(bool noisy = false);    // anonymous new participant
    // ONE row (clicking the name, "Only this line"): by utterance id, so a row number that
    // changes while the panel is open (filter) cannot take another line.
    Q_INVOKABLE bool moveUtteranceToSpeaker(const QString& utteranceId, const QString& speakerKey,
                                            bool noisy = false);
    Q_INVOKABLE bool moveUtteranceToPerson(const QString& utteranceId, const QString& personName,
                                           bool noisy = false);
    Q_INVOKABLE bool moveUtteranceToNewParticipant(const QString& utteranceId, bool noisy = false);
    // "Cannot be identified" (key 0, row button) / "identifiable again" (false): one step
    // (SpeakerEditSession::setUtterancesIgnored). The row keeps its name.
    Q_INVOKABLE bool setUtteranceIgnored(const QString& utteranceId, bool ignored);
    Q_INVOKABLE bool setSelectionIgnored(bool ignored);
    Q_INVOKABLE bool setRowIgnored(int row, bool ignored);
    // The selection (or one row) "Looks right"; Shift: "Looks right, not a sample" (J key in Fix).
    Q_INVOKABLE bool confirmSelection(bool asNoisy = false);
    // Sample meter "+N now": the list filters to the samples this speaker got just now (other
    // rows collapse into separators) and scrolls to the first. Empty key / clearSampleFilter: back.
    Q_INVOKABLE bool showSessionSamples(const QString& speakerKey);
    Q_INVOKABLE void clearSampleFilter();
    // "All N lines of <source>": the REMAINING lines of the last per-line move's source go to
    // the target too (merging the two speakers): one more undo step.
    Q_INVOKABLE bool moveRestOfSource();
    // The lane's "Undo" button: undoes the reassignment, or, if a voiceprint was just made from
    // the lane, deletes EXACTLY that voiceprint (the reassignment stays).
    Q_INVOKABLE void undoChange();
    // The lane's "Make voiceprint" button (only with changeVoiceprintOffer).
    Q_INVOKABLE bool createVoiceprintFromChange();
    Q_INVOKABLE void dismissChange();                           // closes the lane (the suggestion goes away too)
    Q_INVOKABLE QString addParticipant(const QString& personName = QString());
    Q_INVOKABLE bool removeParticipant(const QString& speakerKey);
    Q_INVOKABLE bool reassignSpeaker(const QString& speakerKey, const QString& personName,
                                     bool fixVoiceprints);
    Q_INVOKABLE bool revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints);
    Q_INVOKABLE bool mergeSpeakers(const QString& fromKey, const QString& intoKey);
    Q_INVOKABLE bool confirmRow(int row);                       // "Looks right"
    // "Looks right, but don't use as a sample": confirmation + "overlapping speech" flag.
    Q_INVOKABLE bool confirmRowNoisy(int row);
    // Manual setting of the "overlapping speech" flag (false: "Usable as a sample").
    Q_INVOKABLE bool setRowNoisy(int row, bool noisy);
    Q_INVOKABLE bool setUtteranceNoisy(const QString& utteranceId, bool noisy);
    // Re-check from the audio of confirmed / corrected lines. { ran, flagged,
    // speakersWithConfirmedCore, confirmedLines, referenceSummary, blocker } (referenceSummary:
    // "Reference: ..." if a stored voiceprint took part; see RecheckService::referenceSummary).
    // If it found doubtful lines, the "Uncertain" filter turns on.
    Q_INVOKABLE QVariantMap recheckSpeakers();
    Q_INVOKABLE bool acceptSuggestion();
    Q_INVOKABLE void dismissSuggestion();
    // Pairwise review ("Review A and B"): only the two speakers' lines, based only on their
    // voices. { ran, flagged, blocker, fallbackA, fallbackB, centroidSimilarity,
    // referenceSummary, message }; the message ends with the reference composition if a voiceprint was used.
    // If it found doubtful lines, the "Uncertain" filter turns on; the result (or the blocker)
    // is also announced as a notice.
    Q_INVOKABLE QVariantMap recheckPair(const QString& speakerKeyA, const QString& speakerKeyB);
    // The lane offer: "Review" (the above, for the offer's two speakers) / "Not now" (does not
    // ask again for this pair in this session).
    Q_INVOKABLE QVariantMap acceptPairOffer();
    Q_INVOKABLE void declinePairOffer();
    // Possible partners for the pairwise review: the meeting's other NAMED speakers that have
    // lines (a different person): { key, name, colorIndex, utteranceCount }.
    Q_INVOKABLE QVariantList pairCandidates(const QString& speakerKey) const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ---- copy (clipboard) -------------------------------------------------
    // The rows' text with the speaker's name and timestamp; a new header line on speaker change.
    Q_INVOKABLE QString textOfRows(const QVariantList& rows) const;
    Q_INVOKABLE QString selectionText() const;
    // Puts it on the clipboard; returns how many utterances were copied (0 = nothing).
    Q_INVOKABLE int copyRow(int row);
    Q_INVOKABLE int copySelection();
    Q_INVOKABLE int copyAll();
    Q_INVOKABLE void selectAll();

    // ---- "Next uncertain" -------------------------------------------
    // Steps to the first uncertain row after `fromRow` (direction < 0: before it), wrapping
    // around, selects it and scrolls there. Returns the row; -1 = no uncertain row.
    Q_INVOKABLE int stepUncertain(int fromRow, int direction = 1);

    // ---- "Listen": representative sample of a speaker ----------------
    // { ok, startMs, endMs }: a longer, non-uncertain utterance of the speaker
    // (trimmed to at most ~12 s).
    Q_INVOKABLE QVariantMap speakerSample(const QString& speakerKey) const;

    // ---- speaker / voiceprint -------------------------------------------
    Q_INVOKABLE QVariantMap speakerInfo(const QString& speakerKey) const;
    // The meeting's speakers filtered by the search text (accent- and case-insensitive, like the
    // person list), without `excludeKey`; same elements as `speakers`.
    Q_INVOKABLE QVariantList speakersMatching(const QString& query, const QString& excludeKey) const;
    // The person's speaker key in this meeting; empty if not a participant.
    Q_INVOKABLE QString speakerKeyForPerson(const QString& personName) const;
    // Hungarian definite article before a number: true -> "az" (1, 5, 50..., 1000...), else "a".
    Q_INVOKABLE static bool needsAz(int number);
    // { supported, reason, usableLines, usableSec, missingSec, sufficient }; reason: why audio
    // analysis is unavailable ("model" = no voice model, "audio" = no mixed-down audio).
    Q_INVOKABLE QVariantMap voiceprintMaterial(const QString& speakerKey) const;
    // { ok, message, printId, usedLines, usedSec, missingSec }; made only on explicit request.
    Q_INVOKABLE QVariantMap createVoiceprint(const QString& speakerKey);
    // The line popup's "Voiceprint sample from this line" button. lineSampleInfo:
    // { ok, reason, seconds, personName }: the button's state and explanation.
    Q_INVOKABLE QVariantMap lineSampleInfo(const QString& utteranceId) const;
    Q_INVOKABLE QVariantMap createVoiceprintFromLine(const QString& utteranceId);
    // Undoes the voiceprint just made: exactly that voiceprint is deleted.
    Q_INVOKABLE bool removeVoiceprint(const QString& printId);

    // ---- search ----------------------------------------------------------
    // Steps to the next / previous hit; returns its row (-1 = no hit).
    Q_INVOKABLE int searchStep(int direction);

    // ---- people (read by PersonListModel) ---------------------------------
    QVector<tanara::editing::PersonInfo> people() const;
    bool isMeetingPerson(const QString& name) const;

    // ---- evidence, candidates, groups, lane assignment -------------------
    // "Why not X?" (evidence against the row's current speaker, with a fix link).
    Q_INVOKABLE QVariantList whyNot(const QString& utteranceId) const;
    // "Why this one?" for the whole speaker.
    Q_INVOKABLE QVariantList speakerEvidence(const QString& speakerKey) const;
    // Row data for the panel opened from a "To review" row: { utteranceId, speakerKey,
    // timeLabel, startMs, endMs } (like rowInfo).
    Q_INVOKABLE QVariantMap lineInfo(const QString& utteranceId) const;
    // "Which track does the speaker use?": the meeting's tracks { id, name, kind ("mic" |
    // "loopback" | "other"), checked }; empty if the meeting is single-track (nothing to assign).
    Q_INVOKABLE QVariantList trackOptions(const QString& speakerKey) const;
    // Manual track assignment (one undo step; empty list = clear the constraint).
    Q_INVOKABLE bool setSpeakerTracks(const QString& speakerKey, const QStringList& trackIds);
    // "learned: usually on the call audio"; empty if there is no learned / manual basis.
    Q_INVOKABLE QString sideBasisText(const QString& speakerKey) const;
    // Quick "+ #Tag" (E1 candidate row, E3 tag row): the meeting's first tag the person does
    // not have yet: { tagId, name, label: "+ #Nordvik" }; empty if none (no tag on the meeting,
    // anonymous / own person, or every tag is already on them).
    Q_INVOKABLE QVariantMap quickTagFor(const QString& personName) const;
    // Puts the tag on the person (TagService::addPersonTag: one step on the tag stack, undone by
    // the shell's "tags" undo); candidates / evidence are then recomputed.
    // On success no notice: the caller shows the undoable toast.
    Q_INVOKABLE bool addQuickTag(const QString& personName, const QString& tagId);
    // Applies the group's suggestion (one undo step; the change bar reports it). If the group is
    // the queue's opening suggestion, exactly the opening's still-open rows move (what the
    // effect preview showed); noisy: "not a sample" (Shift+Enter), in the same step. The "Just
    // now" row text: "All N -> Y".
    Q_INVOKABLE bool applyReviewGroup(const QString& groupId, bool noisy = false);
    // Pattern offer "Yes": the rows go to an existing speaker in ONE undo step
    // (noisy: not a sample). The "Just now" row text: "All N -> Y".
    Q_INVOKABLE bool moveUtterancesToSpeaker(const QStringList& utteranceIds, const QString& speakerKey,
                                             bool noisy = false);
    // "Skip": the group does not show up again in this session.
    Q_INVOKABLE void skipReviewGroup(const QString& groupId);
    // Splits the contaminated core (one undo step).
    Q_INVOKABLE bool splitSpeaker(const QString& speakerKey);
    // "Looks right" / "Looks right, not a sample" by utterance id (from the "To review" rows).
    Q_INVOKABLE bool confirmUtterance(const QString& utteranceId, bool asNoisy = false);
    // All lines similar to the new person go to them (the SimilarToNewPerson group).
    Q_INVOKABLE bool acceptNewPersonSimilar();
    // "Voiceprint when enough": now if the material suffices, otherwise as soon as it does (explicit request).
    Q_INVOKABLE QVariantMap requestNewPersonVoiceprint();
    // Answer to the new person's tag question (Yes: the tag goes on them; No: lasting refusal);
    // one undo step on the tag stack; pair: from the "One by one..." list.
    Q_INVOKABLE void answerChangeTag(bool yes);
    Q_INVOKABLE void answerChangeTagPair(int index, bool yes);
    // Undoes the answer just given (the tag stack's latest step).
    Q_INVOKABLE void undoChangeTagAnswer();
    // The name of the person (a "Who was there?" participant) for the speaker; empty = anonymous.
    Q_INVOKABLE QString personOf(const QString& speakerKey) const;

    // ---- worklist (TranscriptEditorViewModelQueue.cpp) ----
    ReviewQueueModel* reviewQueue() const { return m_queue; }
    bool queueActive() const;
    // Recomputes the groups now (so the queue starts from fresh ReviewViews).
    void flushPendingReview();
    bool reviewAutoPlay() const;
    void setReviewAutoPlay(bool on);
    // Context rows of the focus card: the `before` utterances before the row and `after` after
    // it, from the transcript (unfiltered, non-uncertain ones too), in time order:
    // [{ utteranceId, offset (-2..+2), speakerKey, speakerName, colorIndex, timeLabel, startMs,
    // endMs, text, uncertain, decidedKind ("corrected" | "confirmed" | "ignored" | "") }].
    // Unknown row: empty list.
    Q_INVOKABLE QVariantList contextLines(const QString& utteranceId, int before, int after) const;
    // "Confirm all": among the queue's self-resolved (alreadyDecided) rows, confirms those at
    // least kMinEmbedMs long in ONE undo step (short ones would not be samples; they are left
    // alone). The "Just now" row: "N lines confirmed". Returns the number of confirmed rows.
    Q_INVOKABLE int confirmSettled();

    // ---- demo states (for screenshots) ---------------------------------
    // "selection" | "suggestion" | "suggestionShown" | "filter" | "search" | "searchEmpty" |
    // "rail" | "changeLine" | "changeSelection" | "changeSpeaker" | "changeFilter" |
    // "changeVoiceprint" | "changeVoiceprintDone" | "voiceprintHas" | "voiceprintNone" |
    // "voiceprintDone" | "voiceprintShort" | "recheck" (the filter after a re-check) |
    // "recheckReady" (every doubtful row decided: the filter button offers the re-check) |
    // "noisy" (an "overlapping speech" row) | "changePairOffer" (the pairwise review offer on
    // the lane after a reassignment) | "imentWarning" / "imentLog" ("Just now" row with a
    // deviating-sample warning, with the decision log) | "shiftPreview" | "ignoredRows" |
    // "sampleFilter". If the audio analysis is still running, it applies at the end.
    Q_INVOKABLE void applyDemoState(const QString& state);
    // Whether a demo state still waits for the end of the audio analysis.
    Q_INVOKABLE bool demoStatePending() const { return !m_pendingDemoState.isEmpty(); }

    // ---- sweep mode ----
    bool sweepActive() const { return m_sweepActive; }
    void setSweepActive(bool on);
    bool sweepContinuous() const { return m_sweepContinuous; }
    void setSweepContinuous(bool on);
    int sweepStopRow() const;
    int sweepAfterRow() const;
    int sweepTargetRow() const;
    int sweepFrozenFirstRow() const;
    int sweepFrozenLastRow() const;
    QString sweepFrozenFrom() const;
    QString sweepFrozenTo() const;
    QStringList pendingMarkerIds() const;
    int rebehindCount() const { return int(m_sweepRebehind.size()); }
    QVariantList rebehindRows() const;
    int sweepAheadCount() const;
    QVariantList sweepSections() const;
    int sweepSectionIndex() const { return m_sweepSection; }
    QVariantMap sweepSectionSummary() const { return m_sweepSummary; }
    QVariantMap sweepDock() const { return m_sweepDockCache; }
    // The row was already decided / skipped / "Later" in the sweep (not a stop; the list model's role).
    bool isSweepHandled(int utterance) const;
    // The visible rows (QML supplies them on scroll); the frozen window is this +/- 2 rows.
    Q_INVOKABLE void setSweepWindow(int firstRow, int lastRow);
    // Decision on the sweep's target row (keys and the decision bar): action = "default" (Enter:
    // the suggestion, else Looks right) | "confirm" (J) | "ignore" (0) | "lane" (1-9, the rail's
    // column) | "later" (right arrow, Later) | "continue" (Space: go on without deciding) |
    // "replay" (left arrow: the row again, the stop stays). noisy = Shift (not a sample).
    // At a stop, playback resumes after the decision (sweepResumeRequested).
    Q_INVOKABLE bool sweepDecide(int row, const QString& action, int lane = -1, bool noisy = false);
    // The next stop row (doubtful, not yet decided, not "Later") after `fromRow`; -1 = none.
    Q_INVOKABLE int sweepNextStopRow(int fromRow) const;
    // Applies the pending markers now (section end, sweep end).
    Q_INVOKABLE void releasePendingMarkers();
    // The summary's buttons: the next section / the rows doubtful again / the start of the
    // skipped ones (ms; -1 = nowhere to go). All three close the summary.
    Q_INVOKABLE int sweepNextSection();
    Q_INVOKABLE int sweepReviewBehind();
    Q_INVOKABLE int sweepReviewLater();
    Q_INVOKABLE void dismissSweepSummary();
    // Section limits (tests, demo): at most this much time / this many doubtful rows per section.
    Q_INVOKABLE void setSweepSectionLimits(int maxMs, int maxUncertain);
    // Demo (screenshot): "sweepStop" | "sweepContinuous" | "sweepSectionDone": a moment of the
    // sweep on the fictional meeting (QML supplies the player position: sweepDemoPositionMs).
    Q_INVOKABLE int stageSweepDemo(const QString& state);

signals:
    void meetingIdChanged();
    void demoVariantChanged();
    void sessionChanged();          // another meeting / new transcript: everything must be re-read
    void speakersChanged();
    void overviewChanged();
    void railVisibleChanged();
    void uncertainOnlyChanged();
    void uncertainCountChanged();
    void recheckStateChanged();
    // A re-check finished (from here or from the shell, on the same editor).
    void recheckFinished(int flagged, int speakersWithConfirmedCore, int confirmedLines);
    void selectionChanged();
    void undoStateChanged();
    void suggestionChanged();
    void changeChanged();
    void voiceStateChanged();
    void searchChanged();
    void highlightColorChanged();
    void playingRowChanged();
    void peopleChanged();
    void reviewChanged();
    void decisionsChanged();
    void sampleDeviationChanged();
    void sampleStatsChanged();
    void ignoredCountChanged();
    void listFilterChanged();
    void queueActiveChanged();      // S3
    // ---- sweep mode ----
    void sweepChanged();
    void sweepWindowChanged();
    void sweepMarkersChanged();
    void sweepSectionsChanged();
    void sweepDockChanged();
    void sweepStopReached(int row);         // stop mode: QML stops the player
    void sweepResumeRequested();            // decision at the stop: playback resumes
    void sweepSectionEnded(int index);      // reached the section end: summary, the player stops
    void reviewAutoPlayChanged();   // S4
    // QML should scroll to this row (search, "Show me", suggestion, demo).
    void revealRequested(int row);
    // Short, non-modal feedback (QML hands it to shell.toast, if present).
    void notice(const QString& text);

private:
    void resolveFacade();
    void attachFacade(tanara::editing::ITranscriptEditorFacade* facade);
    void attach(tanara::editing::SpeakerEditSession* session, tanara::editing::RecheckService* recheck);
    void detach();
    void reloadAll();
    void startEmbeddingIfNeeded();
    void onUtterancesChanged(const QStringList& ids);
    void onSpeakersChanged();
    void onSuggestionChanged();
    void rebuildSpeakers(bool recomputeCollapsed);
    void rebuildOverview();
    void scheduleOverview();
    void updateVoiceNote();
    void updateRecheckState();
    void updateSearch();
    void updatePlayingRow();
    void setSelection(const QSet<int>& selection, int anchor);
    QStringList selectedIds() const;
    QString textOfUtterances(QVector<int> utterances) const;
    int copyUtterances(const QVector<int>& utterances);
    QStringList idsOfRows(int fromRow, int toRow) const;
    QString laneKey(int lane) const;
    void afterMove(const QString& targetKey);
    enum class MoveTarget { Speaker, Person, NewParticipant };
    // Shared path of the per-line moves: action + column expansion + notice bar.
    // Returns the target speaker's key (empty = nothing happened).
    QString moveLines(const QStringList& ids, MoveTarget kind, const QString& value, bool noisy = false);
    // ---- decision log ("Just now" row) ----
    struct Decision {
        QString text;
        QString time;
        QString utteranceId;
        QVariantList chips;
        quint64 serial = 0;         // the editor's undo step (0: feedback, cannot be undone)
        bool undone = false;
        QString verb;               // "move" | "keep" | "ignore" | "" (the Just now row's icon)
    };
    // Before / after a decision: for computing the side effect (suggestions for other rows).
    void beginDecision(const QStringList& ids);
    void endDecision(const QString& text, const QStringList& ids, const QString& kind);
    void pruneDecisions();
    void updateSampleDeviation();
    void updateSampleStats();
    void updateIgnoredCount();
    QVector<Decision> m_decisions;      // in time order (newest last)
    bool m_tracking = false;
    QSet<QString> m_trackIds;           // the decided rows
    QHash<QString, QString> m_trackKeys;    // the decided rows' speaker before the decision
    QSet<QString> m_sideChanged;        // other rows whose suggestion / doubt changed
    QVariantMap m_sampleDeviation;
    QVariantList m_sampleStats;
    int m_ignoredCount = 0;
    bool m_hideIgnored = false;
    QString m_sampleFilterKey;
    QSet<int> m_sampleFilter;
    void publishChange(const QString& text, const QString& sourceKey, const QString& targetKey,
                       int lastUtterance, bool offerVoiceprint = false, bool newPerson = false);
    void publishWholeSpeakerChange(const QString& fromName, int lines, const QString& targetKey);
    bool canOfferVoiceprint(const QString& speakerKey) const;
    QString voiceprintMessage(const QString& name, const tanara::editing::VoiceprintResult& result) const;
    void clearChange();
    bool loadRailState() const;
    void saveRailState() const;
    void applyPendingDemoState();
    void stageS2Demo(const QString& state);
    void rebuildReview();
    void scheduleReview();
    void updateSimilar();
    QString tagLabel(const QString& id) const;
    QString sideOfTrackKind(const QString& trackId) const;

    bool m_deferred = false;
    bool m_legacyTranscript = false;
    QString m_legacyText;
    QString m_meetingId;
    QString m_demoVariant;
    QPointer<tanara::editing::ITranscriptEditorFacade> m_facade;
    bool m_facadeInjected = false;
    const void* m_facadeSet = nullptr;  // the services set the facade came from (AppContext)
    QString m_facadeVariant;            // the demoVariant it was asked for
    QPointer<tanara::editing::SpeakerEditSession> m_session;
    QPointer<tanara::editing::RecheckService> m_recheck;
    bool m_sessionInjected = false;
    std::function<QVector<tanara::editing::PersonInfo>()> m_peopleProvider;
    QString m_uiStatePath;
    bool m_uiStatePathSet = false;

    TranscriptListModel* m_rows = nullptr;
    QVector<tanara::editing::EditorUtterance> m_utts;
    QHash<QString, int> m_uttIndex;
    QVector<tanara::editing::EditorSpeaker> m_speakers;
    QHash<QString, SpeakerView> m_views;
    QStringList m_laneKeys;             // keys of the visible columns, in order
    QSet<QString> m_collapsed;          // collapsed (rarely speaking) keys
    bool m_lanesExpanded = false;
    int m_collapsedShown = 0;
    QVariantList m_lanes;
    QVariantList m_speakerList;
    QVariantList m_overview;
    qint64 m_durationMs = 0;
    qint64 m_timelineMs = 0;
    QTimer m_overviewTimer;

    bool m_railVisible = false;
    bool m_uncertainOnly = false;
    int m_uncertainCount = 0;
    bool m_canRecheck = false;
    QString m_recheckBlocker;
    QSet<int> m_selected;
    int m_anchor = -1;

    QSet<int> m_suggested;
    int m_suggestionAnchor = -1;
    QString m_suggestionTargetName;
    bool m_suggestionShown = false;
    tanara::editing::PairRecheckOffer m_pairOffer;
    bool m_demoPairOffer = false;       // the demo state's fictional offer (not the editor's)

    struct Change {
        bool active = false;
        QString text;
        QString sourceKey;
        QString targetKey;
        int utterance = -1;
        int restCount = 0;
        QString restText;
        // State of the voiceprint offer: none / offerable / just made / undone.
        enum Voiceprint { None, Offer, Created, Removed };
        Voiceprint voiceprint = None;
        QString printId;                // the voiceprint made from the lane (for its undo)
        bool newPerson = false;         // the reassignment created a new person
        // New person on a tagged meeting: the learning question and the answer text.
        QVector<tanara::library::PersonTagQuestion> tagQuestions;
        QString tagAnswer;
    };
    Change m_change;
    int m_changeSerial = 0;
    int m_opDepth = 0;                  // > 0: own action running (we write the lane out afterwards)
    QSet<int> m_sticky;

    qreal m_embeddingProgress = 0.0;
    bool m_cancelRequested = false;
    bool m_embeddingFailed = false;
    QString m_voiceNote;

    QString m_searchQuery;
    mutable QVector<QString> m_folded;
    QVector<int> m_matches;
    QSet<int> m_matchSet;
    int m_searchCurrent = -1;
    QColor m_highlight{QStringLiteral("#fcedcd")};
    QColor m_highlightCurrent{QStringLiteral("#eac992")};

    int m_playbackMs = 0;
    bool m_playbackActive = false;
    int m_playingUtt = -1;
    int m_playingRow = -1;

    QString m_pendingDemoState;

    // ---- v3 ----
    QVector<ReviewView> m_reviewViews;
    QSet<QString> m_skippedGroups;
    int m_reviewCount = 0;
    int m_reviewGroupCount = 0;
    QVariantMap m_contaminated;
    QSet<QString> m_sideConflicts;          // ids of the lane-contradicting rows
    QHash<QString, QString> m_speakerSides; // speaker key -> "local" | "remote" | "mixed" | "unknown"
    QString m_newPersonKey;
    void askNewPersonTag(const QString& name);
    void answerTagQuestion(const tanara::library::PersonTagQuestion& q, bool yes);
    QString m_newPersonGroupId;
    QSet<int> m_similar;
    bool m_similarShown = false;
    bool m_similarAutoShown = false;
    QVariantList m_similarMarks;
    QString m_voiceprintWhenReady;          // "Voiceprint when enough" request for this speaker
    QTimer m_reviewTimer;

    // ---- worklist (TranscriptEditorViewModelQueue.cpp) ----
    ReviewQueueModel* m_queue = nullptr;
    QString m_queueDemo;                    // queue demo waiting for the end of audio / background analysis
    void initQueue();                       // from the constructor
    // "reviewQueue*" demo states; true if handled.
    bool applyQueueDemoState(const QString& state);
    void tryQueueDemo();
    // Executes the queue's opening suggestion (called by applyReviewGroup); false if the
    // groupId is not an opening (then the group path runs).
    bool applyQueueOpener(const QString& groupId, bool noisy, bool* handled);
    // The "Just now" text of the group decision ("All N -> Y"), overwriting the last decision.
    void relabelGroupDecision(int count, const QString& targetKey);
    // ---- sweep mode (at the end of TranscriptEditorViewModel.cpp, in its own block) ----
    struct SweepMarker {
        bool uncertain = false;
        QString reason;
        QString likely;
    };
    struct SweepSection {
        int firstUtt = 0;
        int lastUtt = 0;
        qint64 startMs = 0;
        qint64 endMs = 0;
    };
    // Keeps the marker of the row just read from the editor (m_utts[i]) at its old value in the
    // frozen window; a turn to doubtful behind it goes into the "doubtful again" list (called by onUtterancesChanged).
    void sweepFilterIncoming(int i, const tanara::editing::EditorUtterance& before);
    void sweepOnPosition();                 // called by setPlaybackPosition
    void sweepReset();                      // called by reloadAll
    void sweepReleaseOutside();
    void sweepUpdateWindow();
    void sweepComputeSections();
    void sweepEndSection(int index);
    void sweepScheduleDock();
    void sweepRebuildDock();
    bool sweepInWindow(int utt) const;
    bool sweepIsStop(int utt) const;
    int sweepSectionOf(int utt) const;
    QHash<QString, int> sweepSampleSnapshot() const;
    bool m_sweepActive = false;
    bool m_sweepContinuous = false;
    bool m_sweepUserEdit = false;           // undo / redo: markers change immediately
    bool m_sweepReleasing = false;
    int m_sweepStopUtt = -1;
    int m_sweepWatchUtt = -1;
    int m_sweepAfterUtt = -1;
    int m_sweepViewFirst = -1;              // the visible area (rows)
    int m_sweepViewLast = -1;
    int m_sweepWinFirstUtt = -1;            // the frozen window (utterance indices)
    int m_sweepWinLastUtt = -1;
    int m_sweepLastMs = -1;
    QHash<QString, SweepMarker> m_sweepHeld;    // the (old) marker shown in the frozen window
    QStringList m_sweepRebehind;
    QSet<QString> m_sweepHandled;           // decided / skipped / "Later" (does not stop again)
    QSet<QString> m_sweepDecided;
    QSet<QString> m_sweepIgnored;
    QSet<QString> m_sweepLater;
    QSet<QString> m_sweepBackground;        // became doubtful in the background (ahead of you)
    QVector<SweepSection> m_sweepSectionList;
    QSet<int> m_sweepDone;
    int m_sweepSection = -1;
    qint64 m_sweepSectionMaxMs = 12 * 60 * 1000;
    int m_sweepSectionMaxUncertain = 20;
    QHash<QString, int> m_sweepSamplesAtStart;
    QVariantMap m_sweepSummary;
    QTimer m_sweepDockTimer;
    QVariantMap m_sweepDockCache;
    bool m_reviewAutoPlay = true;           // without a facade (test)
    int m_queueDemoGen = 0;                 // silence wait of the focus demos
    bool m_queueDemoQuiet = false;
};

} // namespace tanara_qml
