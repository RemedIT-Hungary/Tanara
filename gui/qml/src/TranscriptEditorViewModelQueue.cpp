// TranscriptEditorViewModel — az S3 munkalista (ReviewQueueModel) bekötése és demó-állapotai.
// Külön fordítási egység, hogy a VM fő fájljában csak néhány jól körülhatárolt hívás legyen
// (konstruktor, reloadAll, onUtterancesChanged, onSpeakersChanged, rebuildReview, demó-elágazás).
#include "ReviewQueueModel.h"
#include "TranscriptDemoSession.h"
#include "TranscriptEditorViewModel.h"
#include "AppContext.h"
#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"

#include <QTimer>

#include <algorithm>

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

// S4: az auto-lejátszás kapcsolója a settings.json-ban (reviewAutoPlay); AppController nélkül
// (demó, teszt) csak a memóriában — a felhasználó beállításaihoz így teszt sosem nyúl.
bool TranscriptEditorViewModel::reviewAutoPlay() const
{
    const tanara::AppController* c = m_controller ? m_controller.data() : AppContext::instance()->controller();
    if (c && c->settings() && !demo()) return c->settings()->settings().reviewAutoPlay;
    return m_reviewAutoPlay;
}

void TranscriptEditorViewModel::setReviewAutoPlay(bool on)
{
    if (on == reviewAutoPlay()) return;
    tanara::AppController* c = m_controller ? m_controller.data() : AppContext::instance()->controller();
    if (c && c->settings() && !demo()) {
        tanara::AppSettings s = c->settings()->settings();
        s.reviewAutoPlay = on;
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

// "reviewQueue"          a v3 „reviewExpanded" előkészítés + a munkalista indul (1. kör)
// "reviewQueueProgress"  + az első három sor eldöntve (javaslat / Jó így nem minta / nem
//                        azonosítható), a negyedik Később → a lista végére
// "reviewQueueSummary"   minden sor eldöntve (a javaslatra / Jó így; egy nem azonosítható, egy
//                        Később) → kör-zárás
// Más változatban (pl. "many") a v3 előkészítés elmarad, a munkalista a meglévő kétes sorokból indul.
//
// S4 — a fókusz-nézet (ReviewFocusView) állapotai a v3 meetingen (szennyezett mag nélkül):
// "reviewFocus"               a munkalista indul, a kurzor az első javaslattal bíró soron
// "reviewFocusDecided"        + a kurzor sora a javaslatra (Enter) → „Imént" sor + a következő kártya
// "reviewFocusIncoming"       a munkalista az utolsó 3 kétes sor nélkül indul; a háttér-elemzés
//                             ezeket „+3 új vár"-ként hozza; két sor eldöntve
// "reviewFocusSummary"        mint a reviewFocusIncoming, majd minden sor eldöntve → kör-zárás
// "reviewFocusAlreadyDecided" a kurzor sora a háttérben már nem kétes („már eldőlt")
bool TranscriptEditorViewModel::applyQueueDemoState(const QString& state)
{
    const bool focus = state.startsWith(QLatin1String("reviewFocus"));
    if (!state.startsWith(QLatin1String("reviewQueue")) && !focus) return false;
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
    // A fókusz-demók csendes időszakot várnak (az utolsó elemzés-frissülés után 700 ms), hogy a
    // képen már ne jöjjön „+N új vár” a pillanatkép után.
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
    // A munkalista egyes sorai nélkül (a háttér-elemzés eredménye változatlan): ezek a sorok
    // nem kétesek a pillanatképben.
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
        m_queue->syncFromReview();      // a valódi elemzés: a 3 sor a beérkezők közé
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
            m_queue->syncFromReview(views, utts);   // a 2. sor a háttérben „eldőlt"
            m_queue->jumpTo(m_queue->indexOf(ids[1]));
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
