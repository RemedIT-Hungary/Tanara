// TranscriptEditorViewModel: wiring of the worklist (ReviewQueueModel) and its demo states.
// A separate translation unit so the VM's main file has only a few well-delimited calls
// (constructor, reloadAll, onUtterancesChanged, onSpeakersChanged, rebuildReview, demo branch).
#include "ReviewQueueModel.h"
#include "TranscriptDemoSession.h"
#include "TranscriptEditorViewModel.h"
#include "AppContext.h"
#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"

#include <QTimer>

#include <algorithm>
#include <limits>

using tanara::editing::EditorUtterance;

namespace tanara_qml {

void TranscriptEditorViewModel::initQueue()
{
    m_queue = new ReviewQueueModel(this);
    connect(m_queue, &ReviewQueueModel::activeChanged, this, &TranscriptEditorViewModel::queueActiveChanged);
    // The queue demo waits for the end of the background analysis (the groups should come from the fresh state).
    connect(this, &TranscriptEditorViewModel::reviewChanged, this, &TranscriptEditorViewModel::tryQueueDemo);
}

bool TranscriptEditorViewModel::queueActive() const
{
    return m_queue && m_queue->active();
}

void TranscriptEditorViewModel::flushPendingReview()
{
    rebuildReview();
}

// The auto-play switch is in settings.json (reviewAutoPlay); without an AppController (demo,
// test) only in memory, so a test never touches the user's settings.
bool TranscriptEditorViewModel::reviewAutoPlay() const
{
    const tanara::app::AppController* c = m_controller ? m_controller.data() : AppContext::instance()->controller();
    if (c && c->settings() && !demo()) return c->settings()->settings().ui.reviewAutoPlay;
    return m_reviewAutoPlay;
}

void TranscriptEditorViewModel::setReviewAutoPlay(bool on)
{
    if (on == reviewAutoPlay()) return;
    tanara::app::AppController* c = m_controller ? m_controller.data() : AppContext::instance()->controller();
    if (c && c->settings() && !demo()) {
        tanara::app::AppSettings s = c->settings()->settings();
        s.ui.reviewAutoPlay = on;
        c->settings()->setSettings(s);
    }
    m_reviewAutoPlay = on;
    emit reviewAutoPlayChanged();
}

QVariantList TranscriptEditorViewModel::contextLines(const QString& utteranceId, int before, int after) const
{
    const int at = m_uttIndex.value(utteranceId, -1);
    if (at < 0) return {};
    QVariantList out;
    for (int i = std::max(0, at - before); i <= std::min(int(m_utts.size()) - 1, at + after); ++i) {
        if (i == at) continue;
        const EditorUtterance& u = m_utts[i];
        const SpeakerView v = m_views.value(u.speakerKey);
        out.append(QVariantMap{
            {QStringLiteral("utteranceId"), u.id},
            {QStringLiteral("offset"), i - at},
            {QStringLiteral("speakerKey"), u.speakerKey},
            {QStringLiteral("speakerName"), v.name},
            {QStringLiteral("colorIndex"), v.colorIndex},
            {QStringLiteral("timeLabel"), timeLabel(u.startMs)},
            {QStringLiteral("startMs"), int(u.startMs)},
            {QStringLiteral("endMs"), int(u.endMs)},
            {QStringLiteral("text"), u.text},
            {QStringLiteral("uncertain"), u.uncertain && !u.confirmed && !u.manuallyCorrected && !u.ignored},
            {QStringLiteral("decidedKind"), u.ignored ? QStringLiteral("ignored")
                                            : u.manuallyCorrected ? QStringLiteral("corrected")
                                            : u.confirmed ? QStringLiteral("confirmed") : QString()},
        });
    }
    return out;
}

// "Confirm all": the self-resolved rows (>= kMinEmbedMs) in one step.
int TranscriptEditorViewModel::confirmSettled()
{
    if (!m_editor || !m_queue || !m_queue->active()) return 0;
    const QStringList ids = m_queue->confirmableIds();
    if (ids.isEmpty()) return 0;
    beginDecision(ids);
    if (!m_editor->confirmUtterances(ids, false)) {
        m_tracking = false;
        return 0;
    }
    endDecision(tr("%n sor megerősítve", nullptr, int(ids.size())), ids, QStringLiteral("sample"));
    return int(ids.size());
}

// ---- opening suggestion, pattern offer ---------------------------------------------

namespace {
// The Hungarian definite article by the number's pronunciation: "az" before a vowel-initial
// numeral (egy, ot..., otven..., ezer...), otherwise "a".
bool articleAz(int n)
{
    if (n == 1 || (n >= 1000 && n < 2000)) return true;
    return QString::number(n).startsWith(QLatin1Char('5'));
}
} // namespace

void TranscriptEditorViewModel::relabelGroupDecision(int count, const QString& targetKey)
{
    if (m_decisions.isEmpty() || count <= 0) return;
    const QString name = m_views.value(targetKey).name;
    m_decisions.last().text = articleAz(count) ? tr("Mind az %1 → %2").arg(count).arg(name)
                                               : tr("Mind a %1 → %2").arg(count).arg(name);
    emit decisionsChanged();
}

bool TranscriptEditorViewModel::applyQueueOpener(const QString& groupId, bool noisy, bool* handled)
{
    *handled = false;
    if (!m_editor || !m_queue || !m_queue->active()) return false;
    QVariantMap op;
    for (const QVariant& v : m_queue->openers()) {
        if (v.toMap().value(QStringLiteral("groupId")).toString() != groupId) continue;
        op = v.toMap();
        break;
    }
    if (op.isEmpty()) return false;
    *handled = true;
    // Exactly the opening's still-open rows (the effect preview was computed from these).
    QStringList ids;
    for (const QString& id : op.value(QStringLiteral("ids")).toStringList()) {
        const int r = m_queue->indexOf(id);
        if (r >= 0 && m_queue->get(r).value(QStringLiteral("state")).toString() == QLatin1String("pending")) ids << id;
    }
    if (ids.isEmpty()) return false;
    const QString key = op.value(QStringLiteral("proposedKey")).toString();
    const QString name = op.value(QStringLiteral("proposedName")).toString();
    const QString target = !key.isEmpty() && m_views.contains(key) ? moveLines(ids, MoveTarget::Speaker, key, noisy)
                         : !name.isEmpty() ? moveLines(ids, MoveTarget::Person, name, noisy)
                         : moveLines(ids, MoveTarget::NewParticipant, QString(), noisy);
    if (target.isEmpty()) return false;
    relabelGroupDecision(int(ids.size()), target);
    rebuildReview();
    return true;
}

bool TranscriptEditorViewModel::moveUtterancesToSpeaker(const QStringList& utteranceIds, const QString& speakerKey,
                                                        bool noisy)
{
    QStringList ids;
    for (const QString& id : utteranceIds)
        if (m_uttIndex.contains(id) && !ids.contains(id)) ids << id;
    if (!m_editor || ids.isEmpty() || speakerKey.isEmpty() || !m_views.contains(speakerKey)) return false;
    if (moveLines(ids, MoveTarget::Speaker, speakerKey, noisy).isEmpty()) return false;
    relabelGroupDecision(int(ids.size()), speakerKey);
    return true;
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
    if (m_demoSession) m_demoSession->stageV3(focus ? state : QStringLiteral("reviewExpanded"));
    setUncertainOnly(true);
    m_queueDemo = state;
    QTimer::singleShot(0, this, &TranscriptEditorViewModel::tryQueueDemo);
    return true;
}

void TranscriptEditorViewModel::tryQueueDemo()
{
    if (m_queueDemo.isEmpty() || !m_editor) return;
    if (reviewRunning() || m_reviewTimer.isActive() || m_editor->isEmbeddingRunning()) return;
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
