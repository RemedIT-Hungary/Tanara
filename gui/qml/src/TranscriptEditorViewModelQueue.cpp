// TranscriptEditorViewModel — az S3 munkalista (ReviewQueueModel) bekötése és demó-állapotai.
// Külön fordítási egység, hogy a VM fő fájljában csak néhány jól körülhatárolt hívás legyen
// (konstruktor, reloadAll, onUtterancesChanged, onSpeakersChanged, rebuildReview, demó-elágazás).
#include "ReviewQueueModel.h"
#include "TranscriptDemoSession.h"
#include "TranscriptEditorViewModel.h"

#include <QTimer>

using tanara::EditorUtterance;

namespace tanara_qml {

void TranscriptEditorViewModel::initQueue()
{
    m_queue = new ReviewQueueModel(this);
    connect(m_queue, &ReviewQueueModel::activeChanged, this, &TranscriptEditorViewModel::queueActiveChanged);
    // A queue-demó a háttér-elemzés végét várja (a csoportok a friss állapotból jöjjenek).
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

// "reviewQueue"          a v3 „reviewExpanded" előkészítés + a munkalista indul (1. kör)
// "reviewQueueProgress"  + az első három sor eldöntve (javaslat / Jó így nem minta / nem
//                        azonosítható), a negyedik Később → a lista végére
// "reviewQueueSummary"   minden sor eldöntve (a javaslatra / Jó így; egy nem azonosítható, egy
//                        Később) → kör-zárás
// Más változatban (pl. "many") a v3 előkészítés elmarad, a munkalista a meglévő kétes sorokból indul.
bool TranscriptEditorViewModel::applyQueueDemoState(const QString& state)
{
    if (!state.startsWith(QLatin1String("reviewQueue"))) return false;
    setRailVisible(true);
    if (m_demoSession) m_demoSession->stageV3(QStringLiteral("reviewExpanded"));
    setUncertainOnly(true);
    m_queueDemo = state;
    QTimer::singleShot(0, this, &TranscriptEditorViewModel::tryQueueDemo);
    return true;
}

void TranscriptEditorViewModel::tryQueueDemo()
{
    if (m_queueDemo.isEmpty() || !m_editor) return;
    if (reviewRunning() || m_reviewTimer.isActive() || m_editor->isEmbeddingRunning()) return;
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
    if (state == QLatin1String("reviewQueueProgress")) {
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
