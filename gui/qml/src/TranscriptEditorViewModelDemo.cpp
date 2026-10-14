// TranscriptEditorViewModel: the design states for the screenshots, the gallery and --demo
// (applyDemoState: "selection", "suggestion", "changeLine", "reviewGroups", "reviewFocus*" ...).
// They script the view model's OWN actions (moves, selection, filters, the change bar) on the
// session the facade serves; the fictional meeting's data and its script-aware preparation
// (confirmed cores, markers, the new person's tag question) are the demo facade's
// (demo/TranscriptEditorDemoFacade: ITranscriptEditorFacade::stage). The sweep's states
// (stageSweepDemo) stay in the sweep block of TranscriptEditorViewModel.cpp.
// A separate translation unit, so the view model's working code carries no demo branches.
#include "DemoText.h"
#include "DemoText.h"
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"

#include <QTimer>

#include <algorithm>
#include <limits>

using namespace tanara;
using tanara::editing::EditorUtterance;

namespace tanara_qml {

// Demo states (on the fictional v3 meeting, after stageV3): "Just now" row with a warning and
// log, rows that cannot be identified + sample marks, the sample meter's filter.
void TranscriptEditorViewModel::stageS2Demo(const QString& state)
{
    if (!m_session || m_utts.size() < 12) return;
    auto keyOfPerson = [this](const QString& name) { return speakerKeyForPerson(name); };
    if (state == QLatin1String("ignoredRows")) {
        // Fixed + sample, Looks right + not a sample, two rows that cannot be identified.
        const QString eszter = keyOfPerson(demoName("Molnár Eszter"));
        if (!eszter.isEmpty()) moveUtteranceToSpeaker(m_utts[3].id, eszter);
        confirmUtterance(m_utts[5].id, /*asNoisy*/ true);
        setUtteranceIgnored(m_utts[2].id, true);
        setUtteranceIgnored(m_utts[6].id, true);
        clearChange();
        emit revealRequested(0);
    } else if (state == QLatin1String("imentWarning") || state == QLatin1String("imentLog")) {
        // A row moved as a sample whose voice differs from the target's other rows: found by trial
        // (and undone), then actually done after a small decision row.
        QString hitId, hitKey;
        for (int i = 9; i < m_utts.size() && hitId.isEmpty(); ++i) {
            const editing::EditorUtterance u = m_utts[i];
            if (u.ignored || u.noisy || u.endMs - u.startMs < 2500) continue;
            for (const QString& key : std::as_const(m_laneKeys)) {
                if (key == u.speakerKey || m_session->speaker(key).anonymous) continue;
                if (!m_session->moveUtterances({u.id}, key)) continue;
                const bool deviates = m_session->lastSampleDeviation().deviates;
                m_session->undo();
                if (deviates) {
                    hitId = u.id;
                    hitKey = key;
                    break;
                }
            }
            if (i > 40) break;
        }
        m_decisions.clear();
        confirmUtterance(m_utts[5].id, false);
        setUtteranceIgnored(m_utts[6].id, true);
        confirmUtterance(m_utts[7].id, /*asNoisy*/ true);
        if (!hitId.isEmpty()) {
            moveUtteranceToSpeaker(hitId, hitKey);
            const int row = m_rows->rowOfUtterance(m_uttIndex.value(hitId, -1));
            if (row >= 0) emit revealRequested(row);
        }
        clearChange();
    } else if (state == QLatin1String("sampleFilter")) {
        const QString gabor = keyOfPerson(demoName("Fehér Gábor"));
        showSessionSamples(gabor.isEmpty() ? m_laneKeys.value(0) : gabor);
    }
}

void TranscriptEditorViewModel::applyDemoState(const QString& state)
{
    m_pendingDemoState = state;
    if (m_session && m_recheck->isEmbeddingRunning()) return;    // at the end of the audio analysis
    applyPendingDemoState();
}

void TranscriptEditorViewModel::applyPendingDemoState()
{
    const QString state = m_pendingDemoState;
    m_pendingDemoState.clear();
    if (state.isEmpty() || !m_session || m_utts.isEmpty()) return;
    if (applyQueueDemoState(state)) return;     // S3: "reviewQueue*"

    if (state == QLatin1String("selection")) {
        setRailVisible(true);
        QSet<int> sel;
        for (int i = 2; i <= 4 && i < m_utts.size(); ++i) sel.insert(i);
        setSelection(sel, 2);
    } else if (state == QLatin1String("suggestion") || state == QLatin1String("suggestionShown")) {
        setRailVisible(true);
        if (m_facade && !m_facade->suggestionSeedUtteranceId().isEmpty()) {
            moveUtteranceToSpeaker(m_facade->suggestionSeedUtteranceId(), m_facade->suggestionTargetKey());
            if (state == QLatin1String("suggestionShown")) setSuggestionShown(true);
            const int row = m_rows->nearestRow(m_suggestionAnchor);
            if (row >= 0) emit revealRequested(row);
        }
    } else if (state == QLatin1String("filter")) {
        setRailVisible(true);
        setUncertainOnly(true);
    } else if (state == QLatin1String("search")) {
        setSearchQuery(demoText("sugo", "help"));
    } else if (state == QLatin1String("searchEmpty")) {
        setSearchQuery(demoText("zsiráf", "giraffe"));
    } else if (state == QLatin1String("rail")) {
        setRailVisible(true);
    } else if (state == QLatin1String("changeLine")) {
        // A row moves to the adjacent column: no similar-row suggestion, only the "all N rows"
        // continuation.
        setRailVisible(true);
        if (m_utts.size() > 3 && m_laneKeys.size() > 1)
            moveUtteranceToSpeaker(m_utts[3].id, m_laneKeys[m_utts[3].speakerKey == m_laneKeys[1] ? 0 : 1]);
    } else if (state == QLatin1String("changeSelection")) {
        setRailVisible(true);
        QSet<int> sel;
        for (int i = 2; i <= 4 && i < m_utts.size(); ++i) sel.insert(i);
        setSelection(sel, 2);
        moveSelectionToSpeaker(m_laneKeys.value(std::min<int>(2, int(m_laneKeys.size()) - 1)));
    } else if (state == QLatin1String("changeSpeaker")) {
        setRailVisible(true);
        reassignSpeaker(m_laneKeys.value(0), demoName("Molnár Eszter"), false);
    } else if (state == QLatin1String("changeVoiceprint") || state == QLatin1String("changeVoiceprintDone")) {
        // An anonymous speaker gets a name (the person has no voiceprint): the lane offers one.
        setRailVisible(true);
        for (const editing::EditorSpeaker& s : std::as_const(m_speakers)) {
            if (!s.anonymous || m_views.value(s.key).lane < 0) continue;
            reassignSpeaker(s.key, demoName("Bálint Péter"), false);
            break;
        }
        if (state == QLatin1String("changeVoiceprintDone")) createVoiceprintFromChange();
    } else if (state == QLatin1String("voiceprintNone") || state == QLatin1String("voiceprintDone")) {
        // Make it a named speaker without a voiceprint (the panel is opened by TranscriptTab).
        bool exists = false;
        for (const editing::EditorSpeaker& s : std::as_const(m_speakers)) exists = exists || (!s.anonymous && !s.hasVoiceprint);
        for (const editing::EditorSpeaker& s : std::as_const(m_speakers)) {
            if (exists || !s.anonymous) continue;
            reassignSpeaker(s.key, demoName("Bálint Péter"), false);
            break;
        }
        dismissChange();
    } else if (state == QLatin1String("voiceprintShort")) {
        // A single short row for a new person: no voiceprint can be made from it.
        if (m_utts.size() > 3) moveUtteranceToPerson(m_utts[3].id, demoName("Bálint Péter"));
        dismissChange();
    } else if (state == QLatin1String("recheck") || state == QLatin1String("recheckReady")) {
        // 3 sure rows confirmed per speaker. recheckReady: the current uncertain ones are decided
        // too ("Looks right") -> the filter button offers the re-check; recheck: it runs as well.
        setRailVisible(true);
        const bool ready = state == QLatin1String("recheckReady");
        QStringList ids = ready ? m_session->uncertainUtteranceIds() : QStringList();
        QHash<QString, int> perSpeaker;
        for (const editing::EditorUtterance& u : std::as_const(m_utts)) {
            if (u.uncertain || u.endMs - u.startMs < 3000) continue;
            if (perSpeaker.value(u.speakerKey) >= 3) continue;
            ++perSpeaker[u.speakerKey];
            ids << u.id;
        }
        m_session->confirmUtterances(ids);
        if (!ready) recheckSpeakers();
    } else if (state == QLatin1String("noisy")) {
        // A longer row gets the "Looks right, but don't use as a sample" flag.
        for (const editing::EditorUtterance& u : std::as_const(m_utts)) {
            if (u.uncertain || u.noisy || u.endMs - u.startMs < 3000) continue;
            m_session->confirmUtterances({u.id}, /*asNoisy*/ true);
            const int row = m_rows->rowOfUtterance(m_uttIndex.value(u.id, -1));
            if (row >= 0) emit revealRequested(row);
            break;
        }
    } else if (state == QLatin1String("changePairOffer")) {
        // A row moves to another named speaker; the lane offers reviewing the two together (the
        // fictional voices are too different for this, so the offer is fictional here).
        setRailVisible(true);
        QString from, to;
        int moved = -1;
        for (int i = 3; i < m_utts.size() && moved < 0; ++i) {
            if (m_session->speaker(m_utts[i].speakerKey).anonymous) continue;
            for (const QString& key : std::as_const(m_laneKeys)) {
                if (key == m_utts[i].speakerKey || m_session->speaker(key).anonymous) continue;
                from = m_utts[i].speakerKey;
                to = key;
                moved = i;
                break;
            }
        }
        if (moved >= 0 && moveUtteranceToSpeaker(m_utts[moved].id, to)) {
            m_session->dismissSuggestion();      // the two never show together
            m_demoPairOffer = true;
            m_pairOffer.sourceSpeakerKey = from;
            m_pairOffer.targetSpeakerKey = to;
            m_pairOffer.centroidSimilarity = 0.82;
            emit suggestionChanged();
        }
    } else if (state == QLatin1String("markers") || state == QLatin1String("fixLinePopover")
               || state == QLatin1String("reviewGroups") || state == QLatin1String("reviewExpanded")
               || state == QLatin1String("contaminatedCore") || state == QLatin1String("speakerWhy")
               || state == QLatin1String("newPersonSimilar") || state == QLatin1String("newPersonTag")
               || state == QLatin1String("imentWarning") || state == QLatin1String("imentLog")
               || state == QLatin1String("shiftPreview") || state == QLatin1String("ignoredRows")
               || state == QLatin1String("sampleFilter")) {
        // Fix mode (on the fictional "v3" meeting): confirmed cores, markers; the panel / opening
        // the group is done by TranscriptTab.
        setRailVisible(true);
        if (m_facade) m_facade->stage(state);     // the new person's tag question too (newPersonTag)
        if (state.startsWith(QLatin1String("review")) || state == QLatin1String("contaminatedCore"))
            setUncertainOnly(true);
        if ((state == QLatin1String("newPersonSimilar") || state == QLatin1String("newPersonTag")) && m_utts.size() > 8) {
            setSelection({6, 7, 8}, 6);
            moveSelectionToPerson(demoName("Nagy Péter"));
            const int row = m_rows->nearestRow(6);
            if (row >= 0) emit revealRequested(row);
        }
        if (state == QLatin1String("markers")) clearChange();
        stageS2Demo(state);
    } else if (state == QLatin1String("changeFilter")) {
        // A row fixed in the filter stays in place ("fixed"), the lane below it.
        setUncertainOnly(true);
        for (const TranscriptListModel::Row& r : m_rows->rows()) {
            if (r.gap) continue;
            const QString from = m_utts[r.utterance].speakerKey;
            for (const QString& key : std::as_const(m_laneKeys)) {
                if (key == from) continue;
                moveUtteranceToSpeaker(m_utts[r.utterance].id, key);
                break;
            }
            break;
        }
    }
}

// "reviewQueue"          the v3 "reviewExpanded" setup + the worklist starts (round 1)
// "reviewQueueProgress"  + the first three rows decided (suggestion / Looks right not a
//                        sample / cannot be identified), the fourth Later -> end of the list
// "reviewQueueSummary"   every row decided (to the suggestion / Looks right; one cannot be
//                        identified, one Later) -> round close
// In another variant (e.g. "many") the v3 setup is skipped, the worklist starts from the existing doubtful rows.
//
// Focus view (ReviewFocusView) states on the v3 meeting (without the contaminated core):
// "reviewFocus"               the worklist starts, the cursor on the first row with a suggestion
// "reviewFocusDecided"        + the cursor row to the suggestion (Enter) -> "Just now" row + the next card
// "reviewFocusIncoming"       the worklist starts without the last 3 doubtful rows; the
//                             background analysis brings them as "+3 new waiting"; two rows decided
// "reviewFocusSummary"        like reviewFocusIncoming, then every row decided -> round close
// "reviewFocusAlreadyDecided" the cursor row is no longer doubtful in the background (the card
//                             shows it normally, Enter = the name stays; the cursor reaches it only with up / left / down)
// "reviewFocusSkipped"        the worklist's 2nd-7th open rows were "self-resolved" in the
//                             background (check mark on the strip, "6 self-resolved · Confirm
//                             all" in the header); the cursor on an open row with a suggestion
//
// Opening suggestion and pattern offer (in the other states the openers are off, so the
// focus card is visible):
// "reviewFocusOpener"         the round start: the first opening suggestion (the v3's 26-row
//                             track contradiction); 1 of the spot-check rows listened to (the view sets it)
// "reviewFocusOpenerDoubt"    the same, 2 listened to, one x -> "One by one" is the default
// "reviewFocusOffer"          the most frequent (reason, current, proposed) triple's 4 rows
//                             to the suggestion -> offer bar under the card; the cursor on the triple's 5th row
// "reviewFocusOfferList"      the same, the "Show me" list expanded (the view sets it)
bool TranscriptEditorViewModel::applyQueueDemoState(const QString& state)
{
    const bool focus = state.startsWith(QLatin1String("reviewFocus"));
    if (!state.startsWith(QLatin1String("reviewQueue")) && !focus) return false;
    const bool opener = state.startsWith(QLatin1String("reviewFocusOpener"));
    m_queue->setOpenerThresholds(opener ? ReviewQueueModel::kOpenerMinLines : std::numeric_limits<int>::max(),
                                 ReviewQueueModel::kOpenerMinVoice);
    setRailVisible(true);
    if (m_facade) m_facade->stage(focus ? state : QStringLiteral("reviewExpanded"));
    setUncertainOnly(true);
    m_queueDemo = state;
    QTimer::singleShot(0, this, &TranscriptEditorViewModel::tryQueueDemo);
    return true;
}

void TranscriptEditorViewModel::tryQueueDemo()
{
    if (m_queueDemo.isEmpty() || !m_session) return;
    if (reviewRunning() || m_reviewTimer.isActive() || m_recheck->isEmbeddingRunning()) return;
    // The focus demos wait for a quiet period (700 ms after the last analysis refresh), so the
    // screenshot gets no "+N new waiting" after the snapshot.
    if (m_queueDemo.startsWith(QLatin1String("reviewFocus")) && !m_queueDemoQuiet) {
        const int gen = ++m_queueDemoGen;
        QTimer::singleShot(700, this, [this, gen] {
            if (gen != m_queueDemoGen) return;
            m_queueDemoQuiet = true;
            tryQueueDemo();
            m_queueDemoQuiet = false;
        });
        return;
    }
    const QString state = m_queueDemo;
    m_queueDemo.clear();
    m_queue->start();
    auto decideCurrent = [this](int how) {
        const QString id = m_queue->currentUtterance();
        if (id.isEmpty()) return false;
        const QString likely = m_queue->current().value(QStringLiteral("likelyKey")).toString();
        if (how == 0 && !likely.isEmpty()) return moveUtteranceToSpeaker(id, likely);
        if (how == 2) return setUtteranceIgnored(id, true);
        return confirmUtterance(id, /*asNoisy*/ how == 1);
    };
    // Without some of the worklist's rows (the background analysis result unchanged): those rows
    // are not doubtful in the snapshot.
    auto withoutIds = [this](const QSet<QString>& drop, QVector<ReviewView>& views, QVector<EditorUtterance>& utts) {
        views = m_reviewViews;
        for (ReviewView& v : views) {
            QStringList keep;
            for (const QString& id : std::as_const(v.ids)) if (!drop.contains(id)) keep << id;
            v.ids = keep;
        }
        utts = m_utts;
        for (EditorUtterance& u : utts) if (drop.contains(u.id)) u.uncertain = false;
    };
    auto pendingIds = [this] {
        QStringList ids;
        for (int i = 0; i < m_queue->count(); ++i) {
            const QVariantMap m = m_queue->get(i);
            if (m.value(QStringLiteral("state")).toString() == QLatin1String("pending"))
                ids << m.value(QStringLiteral("utteranceId")).toString();
        }
        return ids;
    };
    auto jumpToSuggestion = [this] {
        for (int i = 0; i < m_queue->count(); ++i) {
            const QVariantMap m = m_queue->get(i);
            if (m.value(QStringLiteral("state")).toString() == QLatin1String("pending")
                && !m.value(QStringLiteral("likelyName")).toString().isEmpty()) {
                m_queue->jumpTo(i);
                return;
            }
        }
    };
    if (state == QLatin1String("reviewFocus") || state == QLatin1String("reviewFocusDecided")) {
        jumpToSuggestion();
        if (state == QLatin1String("reviewFocusDecided")) decideCurrent(0);
    } else if (state == QLatin1String("reviewFocusIncoming") || state == QLatin1String("reviewFocusSummary")) {
        const QStringList ids = pendingIds();
        QSet<QString> drop;
        for (int i = std::max(0, int(ids.size()) - 3); i < ids.size(); ++i) drop.insert(ids[i]);
        QVector<ReviewView> views;
        QVector<EditorUtterance> utts;
        withoutIds(drop, views, utts);
        m_queue->startFrom(views, utts);
        m_queue->syncFromReview();      // the real analysis: the 3 rows go among the incoming
        jumpToSuggestion();
        decideCurrent(0);
        decideCurrent(1);
        if (state == QLatin1String("reviewFocusSummary")) {
            bool ignoredOne = false, laterOne = false;
            for (int guard = 0; guard < 10000 && m_queue->currentIndex() >= 0; ++guard) {
                if (!laterOne) {
                    m_queue->later();
                    laterOne = true;
                    continue;
                }
                const QString rowState = m_queue->current().value(QStringLiteral("state")).toString();
                if (rowState == QLatin1String("alreadyDecided") || !decideCurrent(ignoredOne ? 0 : 2)) {
                    m_queue->later();
                    continue;
                }
                ignoredOne = true;
            }
        }
    } else if (state == QLatin1String("reviewFocusAlreadyDecided")) {
        const QStringList ids = pendingIds();
        if (ids.size() > 2) {
            QVector<ReviewView> views;
            QVector<EditorUtterance> utts;
            withoutIds({ids[1]}, views, utts);
            m_queue->syncFromReview(views, utts);   // the 2nd row was "decided" in the background
            m_queue->jumpTo(m_queue->indexOf(ids[1]));
        }
    } else if (state == QLatin1String("reviewFocusSkipped")) {
        const QStringList ids = pendingIds();
        QSet<QString> drop;
        for (int i = 1; i < std::min<int>(7, int(ids.size()) - 1); ++i) drop.insert(ids[i]);
        QVector<ReviewView> views;
        QVector<EditorUtterance> utts;
        withoutIds(drop, views, utts);
        m_queue->syncFromReview(views, utts);   // the rows were "self-resolved" in the background
        jumpToSuggestion();
    } else if (state == QLatin1String("reviewFocusOffer") || state == QLatin1String("reviewFocusOfferList")) {
        // Rows of the most frequent (reason, current, proposed) triple; 4 to the suggestion -> pattern.
        QHash<QString, QStringList> byTriple;
        QHash<QString, QString> targetOf;
        QString best;
        for (int i = 0; i < m_queue->count(); ++i) {
            const QVariantMap m = m_queue->get(i);
            const QString likely = m.value(QStringLiteral("likelyKey")).toString();
            const QString speaker = m.value(QStringLiteral("speakerKey")).toString();
            if (m.value(QStringLiteral("state")).toString() != QLatin1String("pending") || likely.isEmpty()
                || likely == speaker)
                continue;
            const QString t = m.value(QStringLiteral("reasonKind")).toString() + QLatin1Char('|') + speaker
                              + QLatin1Char('|') + likely;
            byTriple[t] << m.value(QStringLiteral("utteranceId")).toString();
            targetOf.insert(t, likely);
            if (best.isEmpty() || byTriple.value(t).size() > byTriple.value(best).size()) best = t;
        }
        const QStringList ids = byTriple.value(best);
        if (ids.size() >= 6) {
            for (int k = 0; k < 4; ++k) moveUtteranceToSpeaker(ids[k], targetOf.value(best));
            m_queue->jumpTo(m_queue->indexOf(ids[4]));
        }
    } else if (state == QLatin1String("reviewQueueProgress")) {
        for (int how = 0; how < 3; ++how) decideCurrent(how);
        m_queue->later();
    } else if (state == QLatin1String("reviewQueueSummary")) {
        bool ignoredOne = false, laterOne = false;
        for (int guard = 0; guard < 10000 && m_queue->currentIndex() >= 0; ++guard) {
            if (!laterOne && guard == 1) {
                m_queue->later();
                laterOne = true;
                continue;
            }
            const QString rowState = m_queue->current().value(QStringLiteral("state")).toString();
            if (rowState == QLatin1String("alreadyDecided") || !decideCurrent(ignoredOne ? 0 : 2)) {
                m_queue->later();
                continue;
            }
            ignoredOne = true;
        }
    }
}

} // namespace tanara_qml
