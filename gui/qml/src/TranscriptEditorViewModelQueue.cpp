// TranscriptEditorViewModel: wiring of the worklist (ReviewQueueModel); its demo states are in
// TranscriptEditorViewModelDemo.cpp.
// A separate translation unit so the VM's main file has only a few well-delimited calls
// (constructor, reloadAll, onUtterancesChanged, onSpeakersChanged, rebuildReview).
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"

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

// The auto-play switch is the facade's (settings.json reviewAutoPlay; the demo facade keeps it in
// memory); without a facade (test) only in memory, so a test never touches the user's settings.
bool TranscriptEditorViewModel::reviewAutoPlay() const
{
    return m_facade ? m_facade->reviewAutoPlay() : m_reviewAutoPlay;
}

void TranscriptEditorViewModel::setReviewAutoPlay(bool on)
{
    if (on == reviewAutoPlay()) return;
    if (m_facade) m_facade->setReviewAutoPlay(on);
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
    if (!m_session || !m_queue || !m_queue->active()) return 0;
    const QStringList ids = m_queue->confirmableIds();
    if (ids.isEmpty()) return 0;
    beginDecision(ids);
    if (!m_session->confirmUtterances(ids, false)) {
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
    if (!m_session || !m_queue || !m_queue->active()) return false;
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
    if (!m_session || ids.isEmpty() || speakerKey.isEmpty() || !m_views.contains(speakerKey)) return false;
    if (moveLines(ids, MoveTarget::Speaker, speakerKey, noisy).isEmpty()) return false;
    relabelGroupDecision(int(ids.size()), speakerKey);
    return true;
}

} // namespace tanara_qml
