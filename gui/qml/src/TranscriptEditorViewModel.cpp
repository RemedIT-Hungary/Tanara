#include "TranscriptEditorViewModel.h"

#include "AppContext.h"
#include "TranscriptDemoSession.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/PeopleDirectory.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace tanara;

namespace tanara_qml {

namespace {

// Az összecsukási szabály: a 6 legtöbbet beszélő mindig látszik; azon túl az ennél kisebb
// beszédidő-hányadúak a „+N" oszlopba / „Egyéb (N)" sorba csukódnak.
constexpr int kAlwaysVisibleLanes = 6;
constexpr double kCollapseShare = 0.03;

// Karakterenkénti hajtogatás (kisbetű, ékezet nélkül) — a hossz megmarad, így a találat
// pozíciója visszavetíthető az eredeti szövegre.
QString foldKeepLength(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const QString f = foldForSearch(QString(c));
        out += f.size() == 1 ? f.at(0) : c.toLower();
    }
    return out;
}

// A hanglenyomat-jelző állapota egy beszélőnél (áttekintő, sáv-fejléc).
QString voiceprintState(const EditorSpeaker& s)
{
    if (s.anonymous) return QStringLiteral("anonymous");
    return s.hasVoiceprint ? QStringLiteral("has") : QStringLiteral("none");
}

QVariantMap speakerMap(const EditorSpeaker& s, int lane)
{
    return {
        {QStringLiteral("key"), s.key},
        {QStringLiteral("name"), s.displayName},
        {QStringLiteral("personName"), s.personName},
        {QStringLiteral("rawLabel"), s.rawLabel},
        {QStringLiteral("colorIndex"), s.colorIndex},
        {QStringLiteral("hasVoiceprint"), s.hasVoiceprint},
        {QStringLiteral("anonymous"), s.anonymous},
        {QStringLiteral("added"), s.added},
        {QStringLiteral("isSelf"), s.isSelf},
        {QStringLiteral("utteranceCount"), s.utteranceCount},
        {QStringLiteral("pct"), int(std::lround(s.talkShare * 100.0))},
        {QStringLiteral("voiceConfidence"), s.voiceConfidence},
        {QStringLiteral("lane"), lane},
        {QStringLiteral("voiceprint"), voiceprintState(s)},
    };
}

} // namespace

TranscriptEditorViewModel::TranscriptEditorViewModel(QObject* parent)
    : QObject(parent), m_rows(new TranscriptListModel(this)), m_review(new ReviewGroupsModel(this))
{
    m_overviewTimer.setSingleShot(true);
    m_overviewTimer.setInterval(0);
    connect(&m_overviewTimer, &QTimer::timeout, this, &TranscriptEditorViewModel::rebuildOverview);
    m_reviewTimer.setSingleShot(true);
    m_reviewTimer.setInterval(0);
    connect(&m_reviewTimer, &QTimer::timeout, this, &TranscriptEditorViewModel::rebuildReview);
}

void TranscriptEditorViewModel::componentComplete()
{
    m_deferred = false;
    resolveSession();
}

TranscriptEditorViewModel::~TranscriptEditorViewModel()
{
    detach();
}

// ---- munkamenet -------------------------------------------------------------

void TranscriptEditorViewModel::setMeetingId(const QString& id)
{
    if (m_meetingId == id) return;
    m_meetingId = id;
    emit meetingIdChanged();
    resolveSession();
}

void TranscriptEditorViewModel::setDemoVariant(const QString& variant)
{
    if (m_demoVariant == variant) return;
    m_demoVariant = variant;
    emit demoVariantChanged();
    if (m_demoSession) {
        detach();
        m_demoSession.reset();
    }
    resolveSession();
}

void TranscriptEditorViewModel::setController(AppController* controller)
{
    m_controllerInjected = true;
    if (m_controller == controller) return;
    if (m_controller) m_controller->disconnect(this);
    m_controller = controller;
    resolveSession();
}

void TranscriptEditorViewModel::setPeopleProvider(std::function<QVector<PersonInfo>()> provider)
{
    m_peopleProvider = std::move(provider);
    emit peopleChanged();
}

void TranscriptEditorViewModel::setUiStatePath(const QString& path)
{
    m_uiStatePath = path;
    m_uiStatePathSet = true;
}

void TranscriptEditorViewModel::setEditor(SpeakerEditor* editor)
{
    m_editorInjected = editor != nullptr;
    detach();
    m_demoSession.reset();
    if (editor) attach(editor);
    else reloadAll();
}

void TranscriptEditorViewModel::resolveSession()
{
    if (m_deferred || m_editorInjected) return;
    if (!m_controllerInjected) {
        AppController* c = AppContext::instance()->controller();
        if (m_controller != c) {
            if (m_controller) m_controller->disconnect(this);
            m_controller = c;
        }
    }
    if (m_controller) {
        connect(m_controller, &AppController::peopleChanged,
                this, &TranscriptEditorViewModel::peopleChanged, Qt::UniqueConnection);
        connect(m_controller, &AppController::voiceprintsChanged,
                this, &TranscriptEditorViewModel::peopleChanged, Qt::UniqueConnection);
        if (!m_uiStatePathSet)
            m_uiStatePath = paths::metadataFile(QStringLiteral("ui-transcript.json"),
                                                m_controller->settings()->settings().metadataDir);
        m_demoSession.reset();
        SpeakerEditor* ed = m_meetingId.isEmpty() ? nullptr : m_controller->speakerEditor(m_meetingId);
        const Meeting meeting = ed ? m_controller->store()->load(m_meetingId) : Meeting();
        m_legacyTranscript = ed && !ed->hasTranscript() && meeting.hasTranscript;
        m_legacyText.clear();
        if (m_legacyTranscript) {
            // A régi átirat olvasható marad: a transcript.md szövege (felső korláttal).
            QFile md(QDir(meeting.folder).filePath(QStringLiteral("transcript.md")));
            if (md.open(QIODevice::ReadOnly)) {
                m_legacyText = QString::fromUtf8(md.read(4 * 1024 * 1024));
                // Sima szövegként mutatjuk: a Markdown jelölői (`[00:06]`, **Név**) nélkül.
                m_legacyText.remove(QLatin1Char('`')).remove(QStringLiteral("**"));
            }
        }
        if (ed == m_editor && ed) return;
        detach();
        if (ed) attach(ed);
        else reloadAll();
        return;
    }
    // Nincs controller (demó / képernyőkép / galéria): a beépített kitalált meeting.
    if (m_demoSession) return;
    detach();
    m_demoSession = std::make_unique<TranscriptDemoSession>(m_demoVariant);
    attach(m_demoSession->editor());
}

void TranscriptEditorViewModel::attach(SpeakerEditor* editor)
{
    m_editor = editor;
    connect(editor, &SpeakerEditor::utterancesChanged, this, &TranscriptEditorViewModel::onUtterancesChanged);
    connect(editor, &SpeakerEditor::speakersChanged, this, &TranscriptEditorViewModel::onSpeakersChanged);
    connect(editor, &SpeakerEditor::uncertainCountChanged, this, [this](int n) {
        if (m_uncertainCount == n) return;
        m_uncertainCount = n;
        emit uncertainCountChanged();
    });
    connect(editor, &SpeakerEditor::undoStateChanged, this, [this] {
        // Nem a saját (sávot kiíró) műveletünk: visszavonás, újra, megerősítés, résztvevő… —
        // a legutóbbi átsorolás értesítése már nem érvényes.
        if (m_opDepth == 0) clearChange();
        emit undoStateChanged();
    });
    connect(editor, &SpeakerEditor::suggestionChanged, this, &TranscriptEditorViewModel::onSuggestionChanged);
    connect(editor, &SpeakerEditor::embeddingRunningChanged, this, [this](bool running) {
        if (running) m_embeddingProgress = 0.0;
        emit voiceStateChanged();
        updateRecheckState();
    });
    connect(editor, &SpeakerEditor::recheckFinished, this,
            [this](int flagged, int speakers, int lines) {
        // Talált kétes sort → rögtön a „Bizonytalan" szűrő (ott lehet végiglépkedni rajtuk).
        if (flagged > 0) setUncertainOnly(true);
        emit recheckFinished(flagged, speakers, lines);
    });
    connect(editor, &SpeakerEditor::embeddingProgress, this, [this](int done, int total) {
        m_embeddingProgress = total > 0 ? qreal(done) / qreal(total) : 0.0;
        emit voiceStateChanged();
    });
    connect(editor, &SpeakerEditor::embeddingFinished, this, [this](bool complete) {
        m_embeddingFailed = !complete && !m_cancelRequested;
        m_cancelRequested = false;
        if (complete) m_embeddingProgress = 1.0;
        updateVoiceNote();
        emit voiceStateChanged();
        updateRecheckState();
        applyPendingDemoState();
    });
    connect(editor, &SpeakerEditor::reloaded, this, &TranscriptEditorViewModel::reloadAll);
    // v3: a háttér-elemzés (sáv-oldal, jelöltek, Átnézendő csoportok) végén.
    connect(editor, &SpeakerEditor::reviewGroupsChanged, this, &TranscriptEditorViewModel::rebuildReview);
    connect(editor, &SpeakerEditor::reviewRunningChanged, this, [this] { emit reviewChanged(); });
    connect(editor, &QObject::destroyed, this, [this] {
        m_editor = nullptr;
        reloadAll();
    });
    reloadAll();
}

void TranscriptEditorViewModel::detach()
{
    if (!m_editor) return;
    m_editor->disconnect(this);
    // A háttérben futó hang-elemzés ne dolgozzon egy már nem látszó meetingen (a kész rész
    // a cache-ben marad; visszatéréskor onnan folytatódik).
    if (!m_editorInjected && m_editor->isEmbeddingRunning()) m_editor->cancelEmbedding();
    m_editor = nullptr;
}

void TranscriptEditorViewModel::reloadAll()
{
    m_utts = m_editor ? m_editor->utterances() : QVector<EditorUtterance>();
    m_uttIndex.clear();
    m_uttIndex.reserve(m_utts.size());
    for (int i = 0; i < m_utts.size(); ++i) m_uttIndex.insert(m_utts[i].id, i);
    m_durationMs = m_utts.isEmpty() ? 0 : m_utts.last().endMs;
    for (const EditorUtterance& u : std::as_const(m_utts)) m_durationMs = std::max(m_durationMs, u.endMs);

    m_selected.clear();
    m_anchor = -1;
    m_suggested.clear();
    m_suggestionAnchor = -1;
    m_suggestionShown = false;
    m_suggestionTargetName.clear();
    m_folded.clear();
    m_playingUtt = -1;
    m_playingRow = -1;
    m_lanesExpanded = false;
    m_embeddingFailed = false;
    m_embeddingProgress = 0.0;
    m_uncertainCount = m_editor ? m_editor->uncertainCount() : 0;
    m_uncertainOnly = false;
    m_sticky.clear();
    m_change = Change();
    m_railVisible = loadRailState();
    m_skippedGroups.clear();
    m_newPersonKey.clear();
    m_newPersonGroupId.clear();
    m_similar.clear();
    m_similarShown = false;
    m_similarAutoShown = false;
    m_similarMarks.clear();
    m_voiceprintWhenReady.clear();
    m_sideConflicts.clear();
    m_speakerSides.clear();

    QElapsedTimer perf;
    perf.start();
    rebuildSpeakers(/*recomputeCollapsed*/ true);
    const qint64 tSpeakers = perf.elapsed();
    m_rows->rebuild();
    const qint64 tRows = perf.elapsed();
    onSuggestionChanged();
    updateSearch();
    const qint64 tSearch = perf.elapsed();
    rebuildOverview();
    const qint64 tOverview = perf.elapsed();
    updateVoiceNote();
    updateRecheckState();
    rebuildReview();
    m_review->reset();
    qCDebug(lcPerf).noquote()
        << QStringLiteral("TranscriptEditorViewModel::reloadAll %1 sor: beszélők %2 ms, sorok %3 ms, "
                          "keresés %4 ms, áttekintés %5 ms, egyéb %6 ms, össz %7 ms")
               .arg(m_utts.size()).arg(tSpeakers).arg(tRows - tSpeakers).arg(tSearch - tRows)
               .arg(tOverview - tSearch).arg(perf.elapsed() - tOverview).arg(perf.elapsed());

    emit sessionChanged();
    emit speakersChanged();
    emit uncertainCountChanged();
    emit uncertainOnlyChanged();
    emit railVisibleChanged();
    emit selectionChanged();
    emit undoStateChanged();
    emit changeChanged();
    emit playingRowChanged();
    emit voiceStateChanged();
    emit peopleChanged();

    startEmbeddingIfNeeded();
}

void TranscriptEditorViewModel::startEmbeddingIfNeeded()
{
    if (!m_editor || m_utts.isEmpty()) return;
    if (!m_editor->embeddingsSupported() || m_editor->embeddingsComplete()
        || m_editor->isEmbeddingRunning())
        return;
    m_cancelRequested = false;
    m_editor->startEmbedding();
}

// ---- a szerkesztő jelei -----------------------------------------------------

void TranscriptEditorViewModel::onUtterancesChanged(const QStringList& ids)
{
    tanara::PerfScope perfScope("TranscriptEditorViewModel::onUtterancesChanged", 10);
    if (!m_editor) return;
    QVector<int> changed;
    changed.reserve(ids.size());
    for (const QString& id : ids) {
        const int i = m_uttIndex.value(id, -1);
        if (i < 0) continue;
        m_utts[i] = m_editor->utterance(id);
        changed.append(i);
        // A szűrőben épp látható, most kézzel javított sor a helyén marad („javítva").
        if (m_uncertainOnly && m_utts[i].manuallyCorrected && m_rows->rowOfUtterance(i) >= 0)
            m_sticky.insert(i);
    }
    if (changed.isEmpty()) return;
    if (m_uncertainOnly) m_rows->syncFilter();
    m_rows->notifyUtterances(changed);
    m_review->refreshUtterances(changed);
    updatePlayingRow();
    scheduleOverview();
    updateRecheckState();
    scheduleReview();       // a csoportokon kívüli kétes sorok csoportja azonnal frissül
}

void TranscriptEditorViewModel::onSpeakersChanged()
{
    tanara::PerfScope perfScope("TranscriptEditorViewModel::onSpeakersChanged", 10);
    const QHash<QString, SpeakerView> before = m_views;
    rebuildSpeakers(/*recomputeCollapsed*/ false);
    bool viewsChanged = before.size() != m_views.size();
    for (auto it = m_views.constBegin(); !viewsChanged && it != m_views.constEnd(); ++it) {
        const auto old = before.constFind(it.key());
        viewsChanged = old == before.constEnd() || old->name != it->name
            || old->colorIndex != it->colorIndex || old->lane != it->lane;
    }
    // Csak akkor nyúlunk a sorokhoz, ha név / szín / oszlop tényleg változott (egy sima
    // áthelyezésnél csak a számlálók módosulnak).
    if (viewsChanged)
        m_rows->notifyAll({TranscriptListModel::SpeakerNameRole, TranscriptListModel::ColorIndexRole,
                           TranscriptListModel::LaneRole, TranscriptListModel::HeadRole,
                           TranscriptListModel::LikelySpeakerNameRole});
    if (m_suggestionAnchor >= 0) {
        const QString name = m_views.value(m_editor ? m_editor->suggestion().targetSpeaker : QString()).name;
        if (name != m_suggestionTargetName) {
            m_suggestionTargetName = name;
            emit suggestionChanged();
        }
    }
    // Az új személy oszlopa megszűnt (pl. visszavonás).
    if (!m_newPersonKey.isEmpty() && !m_views.contains(m_newPersonKey)) {
        m_newPersonKey.clear();
        m_rows->notifyAll({TranscriptListModel::NewPersonRole});
        scheduleReview();
    }
    // „Hanglenyomat, ha elég": amint elég az anyag, a kért lenyomat elkészül.
    if (!m_voiceprintWhenReady.isEmpty() && m_editor) {
        const EditorSpeaker sp = m_editor->speaker(m_voiceprintWhenReady);
        if (sp.key.isEmpty() || sp.anonymous || sp.hasVoiceprint) {
            m_voiceprintWhenReady.clear();
        } else if (m_editor->voiceprintMaterial(sp.key).sufficient) {
            const QString key = m_voiceprintWhenReady;
            m_voiceprintWhenReady.clear();
            QTimer::singleShot(0, this, [this, key] { createVoiceprint(key); emit reviewChanged(); });
        }
    }
    m_review->refreshAll();
    emit speakersChanged();
    emit reviewChanged();
    scheduleOverview();
    updateRecheckState();
}

void TranscriptEditorViewModel::onSuggestionChanged()
{
    QVector<int> touched;
    for (int i : std::as_const(m_suggested)) touched.append(i);
    const int oldAnchor = m_suggestionAnchor;
    if (oldAnchor >= 0) touched.append(oldAnchor);

    m_suggested.clear();
    m_suggestionAnchor = -1;
    m_suggestionTargetName.clear();
    if (!m_demoPairOffer)
        m_pairOffer = m_editor && m_editor->hasPairOffer() ? m_editor->pairOffer() : tanara::PairRecheckOffer();
    if (m_editor && m_editor->hasSuggestion()) {
        const SpeakerSuggestion s = m_editor->suggestion();
        for (const QString& id : s.utteranceIds) {
            const int i = m_uttIndex.value(id, -1);
            if (i >= 0) m_suggested.insert(i);
        }
        m_suggestionAnchor = m_uttIndex.value(s.anchorUtteranceId, -1);
        m_suggestionTargetName = m_views.value(s.targetSpeaker).name;
        if (m_suggested.isEmpty()) m_suggestionAnchor = -1;
    }
    if (m_suggestionAnchor < 0) m_suggestionShown = false;
    for (int i : std::as_const(m_suggested)) touched.append(i);
    if (m_suggestionAnchor >= 0) touched.append(m_suggestionAnchor);

    if (m_uncertainOnly) m_rows->syncFilter();
    m_rows->notifyUtterances(touched, {TranscriptListModel::SuggestedRole,
                                       TranscriptListModel::SuggestionAnchorRole});
    emit suggestionChanged();
    scheduleOverview();
}

// ---- beszélők / sávok -------------------------------------------------------

void TranscriptEditorViewModel::rebuildSpeakers(bool recomputeCollapsed)
{
    m_speakers = m_editor ? m_editor->speakers() : QVector<EditorSpeaker>();
    // Saját magam elöl; a többiek az első megjelenés (colorIndex) sorrendjében — ez a sorrend
    // szerkesztés közben nem ugrál.
    std::stable_sort(m_speakers.begin(), m_speakers.end(),
                     [](const EditorSpeaker& a, const EditorSpeaker& b) { return a.isSelf && !b.isSelf; });

    QSet<QString> keys;
    for (const EditorSpeaker& s : std::as_const(m_speakers)) keys.insert(s.key);

    if (recomputeCollapsed) {
        m_collapsed.clear();
        QVector<const EditorSpeaker*> byTime;
        for (const EditorSpeaker& s : std::as_const(m_speakers)) byTime.append(&s);
        std::stable_sort(byTime.begin(), byTime.end(), [](const EditorSpeaker* a, const EditorSpeaker* b) {
            return a->talkTimeMs > b->talkTimeMs;
        });
        for (int i = kAlwaysVisibleLanes; i < byTime.size(); ++i)
            if (byTime[i]->talkShare < kCollapseShare && !byTime[i]->isSelf) m_collapsed.insert(byTime[i]->key);
        // Egyetlen beszélőt nem éri meg csoportba csukni (a „+1" oszlop sem keskenyebb érdemben).
        if (m_collapsed.size() < 2) m_collapsed.clear();
    } else {
        // Szerkesztés közben a csoport nem rendeződik át: csak a megszűnt kulcsok esnek ki;
        // az újonnan felvett résztvevő mindig látható oszlopot kap.
        for (auto it = m_collapsed.begin(); it != m_collapsed.end();)
            it = keys.contains(*it) ? std::next(it) : m_collapsed.erase(it);
    }

    // Azonos nevű beszélők (a diarizáció kettévágta ugyanazt a személyt, vagy két nyers
    // beszélőt ugyanarra a névre azonosított): a felületen a nyers címke különbözteti meg őket.
    QHash<QString, int> nameCount;
    for (const EditorSpeaker& s : std::as_const(m_speakers)) ++nameCount[s.displayName];

    m_laneKeys.clear();
    m_views.clear();
    m_lanes.clear();
    m_speakerList.clear();
    for (const EditorSpeaker& s : std::as_const(m_speakers)) {
        const bool visible = m_lanesExpanded || !m_collapsed.contains(s.key);
        int lane = -1;
        if (visible) {
            lane = int(m_laneKeys.size());
            m_laneKeys << s.key;
        }
        m_views.insert(s.key, SpeakerView{s.displayName, s.colorIndex, lane});
        QVariantMap map = speakerMap(s, lane);
        map.insert(QStringLiteral("nameDuplicate"), nameCount.value(s.displayName) > 1);
        map.insert(QStringLiteral("side"), m_speakerSides.value(s.key, QStringLiteral("unknown")));
        map.insert(QStringLiteral("isNew"), !m_newPersonKey.isEmpty() && s.key == m_newPersonKey);
        if (visible) m_lanes.append(map);
        m_speakerList.append(map);
    }
    m_collapsedShown = int(m_collapsed.size());
}

void TranscriptEditorViewModel::setLanesExpanded(bool expanded)
{
    if (m_lanesExpanded == expanded) return;
    m_lanesExpanded = expanded;
    rebuildSpeakers(false);
    m_rows->notifyAll({TranscriptListModel::LaneRole});
    emit speakersChanged();
    rebuildOverview();
}

QString TranscriptEditorViewModel::laneKey(int lane) const
{
    return lane >= 0 && lane < m_laneKeys.size() ? m_laneKeys[lane] : QString();
}

// ---- áttekintő --------------------------------------------------------------

void TranscriptEditorViewModel::scheduleOverview()
{
    if (!m_overviewTimer.isActive()) m_overviewTimer.start();
}

void TranscriptEditorViewModel::setTimelineMs(int ms)
{
    if (m_timelineMs == ms) return;
    m_timelineMs = ms;
    rebuildOverview();
}

void TranscriptEditorViewModel::rebuildOverview()
{
    m_overviewTimer.stop();
    const double total = double(std::max<qint64>({m_durationMs, m_timelineMs, 1}));
    const bool other = !m_lanesExpanded && !m_collapsed.isEmpty();
    const int rowCount = int(m_laneKeys.size()) + (other ? 1 : 0);
    QVector<QList<qreal>> segs(rowCount), marks(rowCount);
    QVector<int> lastEnd(rowCount, -1);     // az utolsó szegmens megszólalás-indexe (összevonáshoz)

    for (int i = 0; i < m_utts.size(); ++i) {
        const EditorUtterance& u = m_utts[i];
        int row = m_views.value(u.speakerKey).lane;
        if (row < 0) row = other ? rowCount - 1 : -1;
        if (row < 0) continue;
        const qreal x = qreal(u.startMs) / total;
        const qreal w = qreal(std::max<qint64>(0, u.endMs - u.startMs)) / total;
        QList<qreal>& s = segs[row];
        // Az egymást követő megszólalások (ugyanaz a beszélő, köztük nincs más) egy sávvá olvadnak.
        if (lastEnd[row] == i - 1 && s.size() >= 2 && x - (s[s.size() - 2] + s.last()) < 0.004) {
            s.last() = x + w - s[s.size() - 2];
        } else {
            s << x << w;
        }
        lastEnd[row] = i;
        if (m_suggestionShown && m_suggested.contains(i)) marks[row] << x << w;
    }

    m_overview.clear();
    double otherShare = 0.0;
    for (const EditorSpeaker& s : std::as_const(m_speakers)) {
        const int lane = m_views.value(s.key).lane;
        if (lane < 0) {
            otherShare += s.talkShare;
            continue;
        }
        m_overview.append(QVariantMap{
            {QStringLiteral("key"), s.key},
            {QStringLiteral("name"), s.displayName},
            {QStringLiteral("colorIndex"), s.colorIndex},
            {QStringLiteral("pct"), int(std::lround(s.talkShare * 100.0))},
            {QStringLiteral("voiceprint"), voiceprintState(s)},
            {QStringLiteral("segments"), QVariant::fromValue(segs[lane])},
            {QStringLiteral("marks"), QVariant::fromValue(marks[lane])},
        });
    }
    if (other) {
        m_overview.append(QVariantMap{
            {QStringLiteral("key"), QString()},
            {QStringLiteral("name"), tr("Egyéb (%1)").arg(m_collapsed.size())},
            {QStringLiteral("colorIndex"), -1},
            {QStringLiteral("pct"), int(std::lround(otherShare * 100.0))},
            {QStringLiteral("voiceprint"), QString()},
            {QStringLiteral("segments"), QVariant::fromValue(segs[rowCount - 1])},
            {QStringLiteral("marks"), QVariant::fromValue(marks[rowCount - 1])},
        });
    }
    emit overviewChanged();
}

// ---- sín / szűrő ------------------------------------------------------------

bool TranscriptEditorViewModel::loadRailState() const
{
    if (m_uiStatePath.isEmpty() || !m_editor) return false;
    QFile f(m_uiStatePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(f.readAll()).object()
        .value(QStringLiteral("rail")).toObject().value(m_editor->meetingId()).toBool(false);
}

void TranscriptEditorViewModel::saveRailState() const
{
    if (m_uiStatePath.isEmpty() || !m_editor || m_demoSession) return;
    QJsonObject root;
    {
        QFile f(m_uiStatePath);
        if (f.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(f.readAll()).object();
    }
    QJsonObject rail = root.value(QStringLiteral("rail")).toObject();
    // Az alapértelmezés a rejtett sín: csak a bekapcsoltakat tároljuk.
    if (m_railVisible) rail.insert(m_editor->meetingId(), true);
    else rail.remove(m_editor->meetingId());
    root.insert(QStringLiteral("rail"), rail);
    QDir().mkpath(QFileInfo(m_uiStatePath).absolutePath());
    QSaveFile out(m_uiStatePath);
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QJsonDocument(root).toJson());
    out.commit();
}

void TranscriptEditorViewModel::setRailVisible(bool visible)
{
    if (m_railVisible == visible) return;
    m_railVisible = visible;
    saveRailState();
    emit railVisibleChanged();
}

void TranscriptEditorViewModel::setUncertainOnly(bool on)
{
    if (m_uncertainOnly == on) return;
    m_uncertainOnly = on;
    m_sticky.clear();       // a szűrő újbóli alkalmazása: a javított sorok már nem maradnak
    m_rows->rebuild();
    updatePlayingRow();
    emit uncertainOnlyChanged();
    emit selectionChanged();
}

// ---- undo / javaslat --------------------------------------------------------

bool TranscriptEditorViewModel::canUndo() const { return m_editor && m_editor->canUndo(); }
bool TranscriptEditorViewModel::canRedo() const { return m_editor && m_editor->canRedo(); }
QString TranscriptEditorViewModel::undoText() const { return m_editor ? m_editor->undoText() : QString(); }
QString TranscriptEditorViewModel::redoText() const { return m_editor ? m_editor->redoText() : QString(); }

void TranscriptEditorViewModel::undo()
{
    if (m_editor) m_editor->undo();
}

void TranscriptEditorViewModel::redo()
{
    if (m_editor) m_editor->redo();
}

void TranscriptEditorViewModel::setSuggestionShown(bool shown)
{
    if (m_suggestionAnchor < 0) shown = false;
    if (m_suggestionShown == shown) return;
    m_suggestionShown = shown;
    // A szűrőben a megmutatott (javasolt) sorok is látszanak, amíg a kiemelés él.
    if (m_uncertainOnly) m_rows->syncFilter();
    QVector<int> touched;
    for (int i : std::as_const(m_suggested)) touched.append(i);
    m_rows->notifyUtterances(touched, {TranscriptListModel::SuggestedRole});
    emit suggestionChanged();
    rebuildOverview();
    if (shown) {
        // A javasolt sorok a sínen látszanak: kapcsoljuk be, és ugorjunk az elsőre.
        setRailVisible(true);
        int first = -1;
        for (int i : std::as_const(m_suggested)) first = first < 0 ? i : std::min(first, i);
        const int row = m_rows->nearestRow(first);
        if (row >= 0) emit revealRequested(row);
    }
}

bool TranscriptEditorViewModel::acceptSuggestion()
{
    if (!m_editor || !m_editor->hasSuggestion()) return false;
    const SpeakerSuggestion sg = m_editor->suggestion();
    int moved = 0, last = -1;
    for (const QString& id : sg.utteranceIds) {
        const int i = m_uttIndex.value(id, -1);
        if (i < 0 || m_utts[i].speakerKey == sg.targetSpeaker) continue;
        ++moved;
        last = std::max(last, i);
    }
    ++m_opDepth;
    const bool ok = m_editor->acceptSuggestion();
    --m_opDepth;
    if (!ok) return false;
    afterMove(sg.targetSpeaker);
    publishChange(tr("%n sor átkerült ide: %1", nullptr, moved).arg(m_views.value(sg.targetSpeaker).name),
                  sg.sourceSpeakerKey, sg.targetSpeaker, last);
    return true;
}

void TranscriptEditorViewModel::dismissSuggestion()
{
    if (m_demoPairOffer) {
        m_demoPairOffer = false;
        m_pairOffer = PairRecheckOffer();
        emit suggestionChanged();
    }
    if (m_editor) m_editor->dismissSuggestion();
}

// ---- páronkénti átnézés -----------------------------------------------------

QString TranscriptEditorViewModel::pairOfferText() const
{
    if (!m_pairOffer.isValid()) return {};
    return tr("%1 és %2 hangja hasonló. Nézzem át kettejük sorait a megerősítettek alapján?")
        .arg(m_views.value(m_pairOffer.sourceSpeakerKey).name, m_views.value(m_pairOffer.targetSpeakerKey).name);
}

QVariantMap TranscriptEditorViewModel::recheckPair(const QString& speakerKeyA, const QString& speakerKeyB)
{
    QVariantMap out;
    out[QStringLiteral("ran")] = false;
    if (!m_editor) return out;
    const QString nameA = m_views.value(speakerKeyA).name;
    const QString nameB = m_views.value(speakerKeyB).name;
    const SpeakerEditor::PairRecheckResult r = m_editor->recheckPair(speakerKeyA, speakerKeyB);
    out[QStringLiteral("ran")] = r.ran;
    out[QStringLiteral("blocker")] = r.blocker;
    if (!r.ran) {
        out[QStringLiteral("message")] = r.blocker;
        if (!r.blocker.isEmpty()) emit notice(r.blocker);
        return out;
    }
    out[QStringLiteral("flagged")] = r.flagged;
    out[QStringLiteral("fallbackA")] = r.fallbackA;
    out[QStringLiteral("fallbackB")] = r.fallbackB;
    out[QStringLiteral("centroidSimilarity")] = r.centroidSimilarity;

    QString text = r.flagged > 0
        ? tr("%n kétséges sor %1 és %2 között — a Bizonytalan szűrőben.", nullptr, r.flagged).arg(nameA, nameB)
        : tr("A megerősített sorok alapján nem találtam kétséges sort %1 és %2 között.").arg(nameA, nameB);
    // Ha valamelyiküknél kevés volt a megerősített sor, a referencia az összes sora (szennyezett lehet).
    QStringList few;
    if (r.fallbackA) few << nameA;
    if (r.fallbackB) few << nameB;
    if (!few.isEmpty())
        text += QLatin1Char(' ')
              + tr("Kevés megerősített sor (%1): az összes sorát vettem alapul.").arg(few.join(QStringLiteral(", ")));
    // Ha tárolt lenyomat is beszállt a referenciába: miből állt össze (pl. „Dompa 3 sor + 1 korábbi lenyomat").
    const QString refs = r.referenceSummary();
    out[QStringLiteral("referenceSummary")] = refs;
    if (!refs.isEmpty()) text += QLatin1Char(' ') + refs;
    if (!std::isnan(r.centroidSimilarity) && r.centroidSimilarity >= tanara::speakeredit::kPairSimilarWarn)
        text += QLatin1Char(' ')
              + tr("A két hang nagyon hasonló (%1), az eredmény bizonytalan — hallgass bele.")
                    .arg(QString::number(r.centroidSimilarity, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')));
    out[QStringLiteral("message")] = text;
    if (r.flagged > 0) setUncertainOnly(true);
    emit notice(text);
    return out;
}

QVariantMap TranscriptEditorViewModel::acceptPairOffer()
{
    if (!m_pairOffer.isValid()) return {{QStringLiteral("ran"), false}};
    const PairRecheckOffer o = m_pairOffer;
    if (m_demoPairOffer) {
        m_demoPairOffer = false;
        m_pairOffer = PairRecheckOffer();
        emit suggestionChanged();
    }
    const QVariantMap out = recheckPair(o.sourceSpeakerKey, o.targetSpeakerKey);
    // Az eredmény értesítésként szól; a sáv (az átsorolás híre + az ajánlat) ezzel lezárul.
    clearChange();
    return out;
}

void TranscriptEditorViewModel::declinePairOffer()
{
    if (m_demoPairOffer) {
        m_demoPairOffer = false;
        m_pairOffer = PairRecheckOffer();
        emit suggestionChanged();
        return;
    }
    if (m_editor) m_editor->declinePairOffer();
}

QVariantList TranscriptEditorViewModel::pairCandidates(const QString& speakerKey) const
{
    QVariantList out;
    const QString self = m_views.contains(speakerKey) && m_editor ? m_editor->speaker(speakerKey).personName : QString();
    for (const EditorSpeaker& s : m_speakers) {
        if (s.key == speakerKey || s.anonymous || s.utteranceCount <= 0) continue;
        if (!self.isEmpty() && s.personName.compare(self, Qt::CaseInsensitive) == 0) continue;
        out.append(QVariantMap{{QStringLiteral("key"), s.key},
                               {QStringLiteral("name"), s.displayName},
                               {QStringLiteral("colorIndex"), s.colorIndex},
                               {QStringLiteral("utteranceCount"), s.utteranceCount}});
    }
    return out;
}

// ---- hang-elemzés -----------------------------------------------------------

bool TranscriptEditorViewModel::voiceAvailable() const
{
    return m_editor && m_editor->embeddingsSupported() && !m_embeddingFailed;
}

bool TranscriptEditorViewModel::embeddingRunning() const
{
    return m_editor && m_editor->isEmbeddingRunning();
}

void TranscriptEditorViewModel::updateVoiceNote()
{
    QString note;
    if (m_editor && !m_utts.isEmpty()) {
        if (!m_editor->embeddingsSupported())
            note = tr("Nincs letöltve a hangmodell, ezért a bizonytalan sorok jelölése, a hasonló sorok "
                      "felajánlása és a kézi hanglenyomat most nem érhető el. A szerkesztés enélkül is működik.");
        else if (m_embeddingFailed)
            note = tr("A megbeszélés lekevert hangja nem érhető el, ezért a bizonytalan sorok jelölése, a "
                      "hasonló sorok felajánlása és a kézi hanglenyomat most nem érhető el. A szerkesztés "
                      "enélkül is működik.");
    }
    m_voiceNote = note;
}

// ---- keresés ----------------------------------------------------------------

void TranscriptEditorViewModel::setSearchQuery(const QString& query)
{
    if (m_searchQuery == query) return;
    m_searchQuery = query;
    updateSearch();
    if (!m_matches.isEmpty()) {
        m_searchCurrent = 0;
        // Az első találat a mostani lejátszási / kijelölési hely után legyen, ha van ilyen.
        m_rows->notifyUtterances({m_matches[0]}, {TranscriptListModel::RichTextRole});
        const int row = m_rows->nearestRow(m_matches[0]);
        emit searchChanged();
        if (row >= 0) emit revealRequested(row);
    }
}

void TranscriptEditorViewModel::updateSearch()
{
    const QString needle = foldKeepLength(m_searchQuery.trimmed());
    m_matches.clear();
    m_matchSet.clear();
    m_searchCurrent = -1;
    if (!needle.isEmpty()) {
        if (m_folded.size() != m_utts.size()) {
            m_folded.resize(m_utts.size());
            for (int i = 0; i < m_utts.size(); ++i) m_folded[i] = foldKeepLength(m_utts[i].text);
        }
        for (int i = 0; i < m_utts.size(); ++i) {
            if (!m_folded[i].contains(needle)) continue;
            m_matches.append(i);
            m_matchSet.insert(i);
        }
    }
    m_rows->notifyAll({TranscriptListModel::RichTextRole});
    emit searchChanged();
}

int TranscriptEditorViewModel::searchStep(int direction)
{
    if (m_matches.isEmpty()) return -1;
    const int n = int(m_matches.size());
    const int previous = m_searchCurrent;
    m_searchCurrent = ((m_searchCurrent < 0 ? (direction >= 0 ? -1 : 0) : m_searchCurrent)
                       + (direction >= 0 ? 1 : -1) + n) % n;
    QVector<int> touched{m_matches[m_searchCurrent]};
    if (previous >= 0 && previous < n) touched.append(m_matches[previous]);
    m_rows->notifyUtterances(touched, {TranscriptListModel::RichTextRole});
    emit searchChanged();
    const int row = m_rows->nearestRow(m_matches[m_searchCurrent]);
    if (row >= 0) emit revealRequested(row);
    return row;
}

QString TranscriptEditorViewModel::richText(int utterance) const
{
    if (!m_matchSet.contains(utterance) || utterance >= m_folded.size()) return QString();
    const QString needle = foldKeepLength(m_searchQuery.trimmed());
    const QString& text = m_utts[utterance].text;
    const QString& folded = m_folded[utterance];
    const bool current = m_searchCurrent >= 0 && m_searchCurrent < m_matches.size()
        && m_matches[m_searchCurrent] == utterance;
    const QString open = QStringLiteral("<span style=\"background-color:%1\">")
        .arg((current ? m_highlightCurrent : m_highlight).name());
    QString out;
    int pos = 0;
    for (;;) {
        const int hit = int(folded.indexOf(needle, pos));
        if (hit < 0) break;
        out += text.mid(pos, hit - pos).toHtmlEscaped();
        out += open + text.mid(hit, needle.size()).toHtmlEscaped() + QStringLiteral("</span>");
        pos = hit + int(needle.size());
    }
    out += text.mid(pos).toHtmlEscaped();
    return out;
}

void TranscriptEditorViewModel::setHighlightColor(const QColor& c)
{
    if (m_highlight == c) return;
    m_highlight = c;
    if (!m_matches.isEmpty()) m_rows->notifyAll({TranscriptListModel::RichTextRole});
    emit highlightColorChanged();
}

void TranscriptEditorViewModel::setHighlightCurrentColor(const QColor& c)
{
    if (m_highlightCurrent == c) return;
    m_highlightCurrent = c;
    if (!m_matches.isEmpty()) m_rows->notifyAll({TranscriptListModel::RichTextRole});
    emit highlightColorChanged();
}

// ---- pozíció / lejátszás ----------------------------------------------------

QString TranscriptEditorViewModel::timeLabel(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

int TranscriptEditorViewModel::utteranceForTime(int ms) const
{
    if (m_utts.isEmpty()) return -1;
    // Az utolsó megszólalás, amely legkésőbb `ms`-kor kezdődik (a sorok időrendben állnak).
    const auto it = std::upper_bound(m_utts.cbegin(), m_utts.cend(), qint64(ms),
                                     [](qint64 t, const EditorUtterance& u) { return t < u.startMs; });
    if (it == m_utts.cbegin()) return 0;
    return int(std::distance(m_utts.cbegin(), it)) - 1;
}

int TranscriptEditorViewModel::rowForTime(int ms) const
{
    return m_rows->nearestRow(utteranceForTime(ms));
}

void TranscriptEditorViewModel::setPlaybackPosition(int ms, bool active)
{
    m_playbackMs = ms;
    m_playbackActive = active;
    updatePlayingRow();
}

void TranscriptEditorViewModel::updatePlayingRow()
{
    int utt = -1;
    if (m_playbackActive && !m_utts.isEmpty() && m_playbackMs >= m_utts.first().startMs)
        utt = utteranceForTime(m_playbackMs);
    const int row = utt >= 0 ? m_rows->rowOfUtterance(utt) : -1;
    if (utt == m_playingUtt && row == m_playingRow) return;
    m_playingUtt = utt;
    m_playingRow = row;
    emit playingRowChanged();
}

int TranscriptEditorViewModel::rowStartMs(int row) const
{
    const int u = m_rows->utteranceOfRow(row);
    return u >= 0 ? int(m_utts[u].startMs) : -1;
}

QString TranscriptEditorViewModel::rowUtteranceId(int row) const
{
    const int u = m_rows->utteranceOfRow(row);
    return u >= 0 ? m_utts[u].id : QString();
}

int TranscriptEditorViewModel::rowEndMs(int row) const
{
    const int u = m_rows->utteranceOfRow(row);
    return u >= 0 ? int(m_utts[u].endMs) : -1;
}

QVariantMap TranscriptEditorViewModel::rowInfo(int row) const
{
    const int u = m_rows->utteranceOfRow(row);
    if (u < 0) return {};
    const EditorUtterance& utt = m_utts[u];
    return {{QStringLiteral("utteranceId"), utt.id},
            {QStringLiteral("speakerKey"), utt.speakerKey},
            {QStringLiteral("timeLabel"), timeLabel(utt.startMs)},
            {QStringLiteral("startMs"), int(utt.startMs)},
            {QStringLiteral("endMs"), int(utt.endMs)}};
}

qreal TranscriptEditorViewModel::rowStartFraction(int row) const
{
    const auto& rows = m_rows->rows();
    if (row < 0 || row >= rows.size() || m_utts.isEmpty()) return 0.0;
    const qreal total = qreal(std::max<qint64>({m_durationMs, m_timelineMs, 1}));
    return qreal(m_utts[rows[row].utterance].startMs) / total;
}

qreal TranscriptEditorViewModel::rowEndFraction(int row) const
{
    const auto& rows = m_rows->rows();
    if (row < 0 || row >= rows.size() || m_utts.isEmpty()) return 0.0;
    const qreal total = qreal(std::max<qint64>({m_durationMs, m_timelineMs, 1}));
    const TranscriptListModel::Row& r = rows[row];
    const int last = r.gap ? r.utterance + r.hidden - 1 : r.utterance;
    return qreal(m_utts[std::clamp(last, 0, int(m_utts.size()) - 1)].endMs) / total;
}

int TranscriptEditorViewModel::timeAtFraction(qreal fraction) const
{
    const qreal total = qreal(std::max<qint64>({m_durationMs, m_timelineMs, 1}));
    return int(std::clamp(fraction, 0.0, 1.0) * total);
}

// ---- kijelölés --------------------------------------------------------------

int TranscriptEditorViewModel::currentRow() const
{
    if (m_selected.isEmpty()) return -1;
    if (m_anchor >= 0 && m_selected.contains(m_anchor)) return m_rows->rowOfUtterance(m_anchor);
    int first = -1;
    for (int i : m_selected) first = first < 0 ? i : std::min(first, i);
    return m_rows->rowOfUtterance(first);
}

void TranscriptEditorViewModel::setSelection(const QSet<int>& selection, int anchor)
{
    QVector<int> touched;
    for (int i : m_selected)
        if (!selection.contains(i)) touched.append(i);
    for (int i : selection)
        if (!m_selected.contains(i)) touched.append(i);
    m_anchor = anchor;
    if (touched.isEmpty()) return;
    m_selected = selection;
    m_rows->notifyUtterances(touched, {TranscriptListModel::SelectedRole});
    emit selectionChanged();
}

void TranscriptEditorViewModel::selectRow(int row, bool toggle, bool range)
{
    const int u = m_rows->utteranceOfRow(row);
    if (u < 0) return;
    if (range && m_anchor >= 0) {
        const int anchorRow = m_rows->rowOfUtterance(m_anchor);
        if (anchorRow >= 0) {
            QSet<int> sel = toggle ? m_selected : QSet<int>();
            for (int r = std::min(anchorRow, row); r <= std::max(anchorRow, row); ++r) {
                const int i = m_rows->utteranceOfRow(r);
                if (i >= 0) sel.insert(i);
            }
            setSelection(sel, m_anchor);
            return;
        }
    }
    if (toggle) {
        QSet<int> sel = m_selected;
        if (sel.contains(u)) sel.remove(u);
        else sel.insert(u);
        setSelection(sel, u);
        return;
    }
    // Sima kattintás: csak ez a sor; az egyetlen kijelölt sorra újra kattintva megszűnik.
    if (m_selected.size() == 1 && m_selected.contains(u)) setSelection({}, -1);
    else setSelection({u}, u);
}

void TranscriptEditorViewModel::selectOnly(int row)
{
    const int u = m_rows->utteranceOfRow(row);
    if (u >= 0) setSelection({u}, u);
}

void TranscriptEditorViewModel::selectRows(int fromRow, int toRow)
{
    QSet<int> sel;
    for (int r = std::min(fromRow, toRow); r <= std::max(fromRow, toRow); ++r) {
        const int i = m_rows->utteranceOfRow(r);
        if (i >= 0) sel.insert(i);
    }
    setSelection(sel, m_rows->utteranceOfRow(fromRow));
}

void TranscriptEditorViewModel::clearSelection()
{
    setSelection({}, -1);
}

int TranscriptEditorViewModel::stepSelection(int delta)
{
    const auto& rows = m_rows->rows();
    if (rows.isEmpty() || delta == 0) return -1;
    int row = currentRow();
    if (row < 0) row = m_playingRow >= 0 ? m_playingRow - (delta > 0 ? 1 : -1) : (delta > 0 ? -1 : int(rows.size()));
    const int step = delta > 0 ? 1 : -1;
    for (int r = row + step; r >= 0 && r < rows.size(); r += step) {
        if (rows[r].gap) continue;
        setSelection({rows[r].utterance}, rows[r].utterance);
        return r;
    }
    return -1;
}

bool TranscriptEditorViewModel::isRowSelected(int row) const
{
    const int u = m_rows->utteranceOfRow(row);
    return u >= 0 && m_selected.contains(u);
}

QStringList TranscriptEditorViewModel::selectedIds() const
{
    QVector<int> sorted(m_selected.cbegin(), m_selected.cend());
    std::sort(sorted.begin(), sorted.end());
    QStringList ids;
    for (int i : std::as_const(sorted)) ids << m_utts[i].id;
    return ids;
}

QStringList TranscriptEditorViewModel::idsOfRows(int fromRow, int toRow) const
{
    QStringList ids;
    for (int r = std::min(fromRow, toRow); r <= std::max(fromRow, toRow); ++r) {
        const int i = m_rows->utteranceOfRow(r);
        if (i >= 0) ids << m_utts[i].id;
    }
    return ids;
}

// ---- műveletek --------------------------------------------------------------

void TranscriptEditorViewModel::afterMove(const QString& targetKey)
{
    // Ha a cél épp összecsukott oszlopban volt, kapjon saját oszlopot (lássa a felhasználó,
    // hova került a sor).
    if (!targetKey.isEmpty() && m_collapsed.remove(targetKey)) {
        rebuildSpeakers(false);
        m_rows->notifyAll({TranscriptListModel::LaneRole});
        emit speakersChanged();
        rebuildOverview();
    }
}

QString TranscriptEditorViewModel::moveLines(const QStringList& ids, MoveTarget kind, const QString& value)
{
    if (!m_editor || ids.isEmpty()) return {};
    // A művelet előtti beszélők: ebből derül ki, hány sor mozdult ténylegesen, és honnan.
    QVector<QPair<int, QString>> before;
    before.reserve(ids.size());
    for (const QString& id : ids) {
        const int i = m_uttIndex.value(id, -1);
        if (i >= 0) before.append({i, m_utts[i].speakerKey});
    }

    const QList<QString> keysBefore = m_views.keys();
    QString key;
    ++m_opDepth;
    switch (kind) {
    case MoveTarget::Speaker:
        if (m_editor->moveUtterances(ids, value)) key = value;
        break;
    case MoveTarget::Person:
        key = m_editor->moveUtterancesToPerson(ids, value);
        break;
    case MoveTarget::NewParticipant:
        key = m_editor->moveUtterancesToNewParticipant(ids);
        break;
    }
    --m_opDepth;
    if (key.isEmpty()) return {};
    afterMove(key);

    int moved = 0, last = -1;
    QString source;
    bool mixed = false;
    for (const auto& b : std::as_const(before)) {
        if (b.second == key) continue;          // már ott volt
        ++moved;
        last = std::max(last, b.first);
        if (source.isEmpty()) source = b.second;
        else if (source != b.second) mixed = true;
    }
    // Új személy jött létre (E4): az oszlopa kiemelt, a sáv a hozzá hasonló sorokat ajánlja.
    const bool created = kind != MoveTarget::Speaker && !keysBefore.contains(key);
    if (created) {
        m_newPersonKey = key;
        m_similarAutoShown = false;
        rebuildSpeakers(false);
        emit speakersChanged();
        m_rows->notifyAll({TranscriptListModel::NewPersonRole});
    }
    if (moved > 0) {
        const QString name = m_views.value(key).name;
        publishChange(created ? tr("Új személy: %1. %n sor átkerült hozzá.", nullptr, moved).arg(name)
                              : tr("%n sor átkerült ide: %1", nullptr, moved).arg(name),
                      mixed ? QString() : source, key, last, false, created);
    }
    if (created) emit reviewChanged();
    return key;
}

bool TranscriptEditorViewModel::moveRowToLane(int row, int lane)
{
    const int u = m_rows->utteranceOfRow(row);
    const QString key = laneKey(lane);
    if (!m_editor || u < 0 || key.isEmpty()) return false;
    const bool wholeSelection = m_selected.size() > 1 && m_selected.contains(u);
    const QStringList ids = wholeSelection ? selectedIds() : QStringList{m_utts[u].id};
    if (moveLines(ids, MoveTarget::Speaker, key).isEmpty()) return false;
    if (wholeSelection) clearSelection();
    return true;
}

bool TranscriptEditorViewModel::moveRowsToLane(int fromRow, int toRow, int lane)
{
    const QString key = laneKey(lane);
    if (!m_editor || key.isEmpty()) return false;
    return !moveLines(idsOfRows(fromRow, toRow), MoveTarget::Speaker, key).isEmpty();
}

bool TranscriptEditorViewModel::moveSelectionToLane(int lane)
{
    return moveSelectionToSpeaker(laneKey(lane));
}

bool TranscriptEditorViewModel::moveSelectionToSpeaker(const QString& speakerKey)
{
    if (!m_editor || m_selected.isEmpty() || speakerKey.isEmpty()) return false;
    if (moveLines(selectedIds(), MoveTarget::Speaker, speakerKey).isEmpty()) return false;
    clearSelection();
    return true;
}

bool TranscriptEditorViewModel::moveSelectionToPerson(const QString& personName)
{
    const QString name = personName.trimmed();
    if (!m_editor || m_selected.isEmpty() || name.isEmpty()) return false;
    if (moveLines(selectedIds(), MoveTarget::Person, name).isEmpty()) return false;
    clearSelection();
    return true;
}

bool TranscriptEditorViewModel::moveSelectionToNewParticipant()
{
    if (!m_editor || m_selected.isEmpty()) return false;
    if (moveLines(selectedIds(), MoveTarget::NewParticipant, QString()).isEmpty()) return false;
    clearSelection();
    return true;
}

bool TranscriptEditorViewModel::moveUtteranceToSpeaker(const QString& utteranceId, const QString& speakerKey)
{
    tanara::PerfScope perfScope("TranscriptEditorViewModel::moveUtteranceToSpeaker", 10);
    if (!m_uttIndex.contains(utteranceId) || speakerKey.isEmpty()) return false;
    return !moveLines({utteranceId}, MoveTarget::Speaker, speakerKey).isEmpty();
}

bool TranscriptEditorViewModel::moveUtteranceToPerson(const QString& utteranceId, const QString& personName)
{
    tanara::PerfScope perfScope("TranscriptEditorViewModel::moveUtteranceToPerson", 10);
    const QString name = personName.trimmed();
    if (!m_uttIndex.contains(utteranceId) || name.isEmpty()) return false;
    return !moveLines({utteranceId}, MoveTarget::Person, name).isEmpty();
}

bool TranscriptEditorViewModel::moveUtteranceToNewParticipant(const QString& utteranceId)
{
    if (!m_uttIndex.contains(utteranceId)) return false;
    return !moveLines({utteranceId}, MoveTarget::NewParticipant, QString()).isEmpty();
}

bool TranscriptEditorViewModel::moveRestOfSource()
{
    if (!m_change.active || m_change.restCount <= 0) return false;
    return mergeSpeakers(m_change.sourceKey, m_change.targetKey);
}

// ---- a legutóbbi átsorolás értesítése (alsó sáv) ----------------------------

bool TranscriptEditorViewModel::needsAz(int number)
{
    // „az" a magánhangzóval kezdődő számnevek előtt: egy, öt…, ötven…, ötszáz…, ezer…
    if (number == 1 || (number >= 1000 && number < 2000)) return true;
    return QString::number(number).startsWith(QLatin1Char('5'));
}

int TranscriptEditorViewModel::changeRow() const
{
    return m_change.active && m_change.utterance >= 0 ? m_rows->rowOfUtterance(m_change.utterance) : -1;
}

void TranscriptEditorViewModel::publishChange(const QString& text, const QString& sourceKey,
                                              const QString& targetKey, int lastUtterance,
                                              bool offerVoiceprint, bool newPerson)
{
    Change c;
    c.active = true;
    c.newPerson = newPerson;
    if (offerVoiceprint && canOfferVoiceprint(targetKey)) c.voiceprint = Change::Offer;
    c.text = text;
    c.targetKey = targetKey;
    c.utterance = lastUtterance;
    // A tömeges folytatás csak akkor ajánlható, ha a forrásnak maradt sora.
    if (!sourceKey.isEmpty() && sourceKey != targetKey) {
        for (const EditorSpeaker& s : std::as_const(m_speakers)) {
            if (s.key != sourceKey || s.utteranceCount <= 0) continue;
            c.sourceKey = sourceKey;
            c.restCount = s.utteranceCount;
            c.restText = (needsAz(s.utteranceCount) ? tr("%1 mind az %n sora", nullptr, s.utteranceCount)
                                                    : tr("%1 mind a %n sora", nullptr, s.utteranceCount))
                             .arg(s.displayName);
        }
    }
    m_change = c;
    ++m_changeSerial;
    emit changeChanged();
}

void TranscriptEditorViewModel::publishWholeSpeakerChange(const QString& fromName, int lines,
                                                          const QString& targetKey)
{
    const QString target = m_views.value(targetKey).name;
    QString text;
    if (lines <= 0 || target.isEmpty()) text = m_editor ? m_editor->undoText() : QString();
    else if (needsAz(lines)) text = tr("%1 mind az %n sora átkerült ide: %2", nullptr, lines).arg(fromName, target);
    else text = tr("%1 mind a %n sora átkerült ide: %2", nullptr, lines).arg(fromName, target);
    // Egy teljes beszélő most kapott (vagy váltott) személyt: ha annak még nincs hanglenyomata
    // és itt van hozzá elég anyag, a sáv felajánlja — magától sosem készül.
    publishChange(text, QString(), targetKey, -1, /*offerVoiceprint*/ true);
}

bool TranscriptEditorViewModel::canOfferVoiceprint(const QString& speakerKey) const
{
    if (!m_editor || !voiceAvailable()) return false;
    const EditorSpeaker s = m_editor->speaker(speakerKey);
    if (s.key.isEmpty() || s.anonymous || s.hasVoiceprint) return false;
    return m_editor->voiceprintMaterial(speakerKey).sufficient;      // kevés anyagnál nem nyaggatunk
}

bool TranscriptEditorViewModel::createVoiceprintFromChange()
{
    if (!m_editor || !changeVoiceprintOffer()) return false;
    const QString name = m_views.value(m_change.targetKey).name;
    const VoiceprintResult r = m_editor->createVoiceprint(m_change.targetKey);
    m_change.utterance = -1;
    if (!r.ok) {
        // Nem sikerült (pl. közben eltűnt a hang): az ajánlat megszűnik, az ok egyszer elhangzik.
        m_change.voiceprint = Change::None;
        emit changeChanged();
        emit notice(r.error);
        return false;
    }
    m_change.voiceprint = Change::Created;
    m_change.printId = r.printId;
    m_change.text = voiceprintMessage(name, r);
    ++m_changeSerial;
    emit changeChanged();
    emit peopleChanged();
    return true;
}

void TranscriptEditorViewModel::clearChange()
{
    if (!m_change.active) return;
    m_change = Change();
    emit changeChanged();
}

void TranscriptEditorViewModel::undoChange()
{
    if (!changeVoiceprintCreated()) {
        undo();
        return;
    }
    // A lenyomat nem része az undo-veremnek: itt pontosan a most készült lenyomat törlődik,
    // az átsorolás (elnevezés) marad — az továbbra is Ctrl+Z-vel vonható vissza.
    const QString name = m_views.value(m_change.targetKey).name;
    if (m_editor) m_editor->removeVoiceprint(m_change.printId);
    m_change.voiceprint = Change::Removed;
    m_change.printId.clear();
    m_change.text = tr("A most készült hanglenyomat törölve: %1").arg(name);
    ++m_changeSerial;
    emit changeChanged();
    emit peopleChanged();
}

void TranscriptEditorViewModel::dismissChange()
{
    clearChange();
    dismissSuggestion();
}

QString TranscriptEditorViewModel::addParticipant(const QString& personName)
{
    if (!m_editor) return QString();
    const QString key = m_editor->addParticipant(personName.trimmed());
    afterMove(key);
    return key;
}

bool TranscriptEditorViewModel::removeParticipant(const QString& speakerKey)
{
    return m_editor && m_editor->removeParticipant(speakerKey);
}

bool TranscriptEditorViewModel::reassignSpeaker(const QString& speakerKey, const QString& personName,
                                                bool fixVoiceprints)
{
    const QString name = personName.trimmed();
    if (!m_editor || name.isEmpty()) return false;
    const QString fromName = m_views.value(speakerKey).name;
    const int lines = speakerInfo(speakerKey).value(QStringLiteral("utteranceCount")).toInt();
    ++m_opDepth;
    const bool ok = m_editor->reassignSpeaker(speakerKey, name, fixVoiceprints);
    --m_opDepth;
    if (!ok) return false;
    const QString target = speakerKeyForPerson(name);
    afterMove(target);
    publishWholeSpeakerChange(fromName, lines, target);
    return true;
}

bool TranscriptEditorViewModel::revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints)
{
    if (!m_editor) return false;
    const QString fromName = m_views.value(speakerKey).name;
    const int lines = speakerInfo(speakerKey).value(QStringLiteral("utteranceCount")).toInt();
    ++m_opDepth;
    const bool ok = m_editor->revertSpeakerToAnonymous(speakerKey, fixVoiceprints);
    --m_opDepth;
    if (!ok) return false;
    publishWholeSpeakerChange(fromName, lines, speakerKey);
    return true;
}

bool TranscriptEditorViewModel::mergeSpeakers(const QString& fromKey, const QString& intoKey)
{
    if (!m_editor) return false;
    const QString fromName = m_views.value(fromKey).name;
    const int lines = speakerInfo(fromKey).value(QStringLiteral("utteranceCount")).toInt();
    ++m_opDepth;
    const bool ok = m_editor->mergeSpeakers(fromKey, intoKey);
    --m_opDepth;
    if (!ok) return false;
    afterMove(intoKey);
    publishWholeSpeakerChange(fromName, lines, intoKey);
    return true;
}

bool TranscriptEditorViewModel::confirmRow(int row)
{
    const int u = m_rows->utteranceOfRow(row);
    return m_editor && u >= 0 && m_editor->confirmUtterances({m_utts[u].id});
}

bool TranscriptEditorViewModel::confirmRowNoisy(int row)
{
    const int u = m_rows->utteranceOfRow(row);
    return m_editor && u >= 0 && m_editor->confirmUtterances({m_utts[u].id}, /*asNoisy*/ true);
}

bool TranscriptEditorViewModel::setRowNoisy(int row, bool noisy)
{
    const int u = m_rows->utteranceOfRow(row);
    return m_editor && u >= 0 && m_editor->setUtterancesNoisy({m_utts[u].id}, noisy);
}

bool TranscriptEditorViewModel::setUtteranceNoisy(const QString& utteranceId, bool noisy)
{
    return m_editor && m_uttIndex.contains(utteranceId)
        && m_editor->setUtterancesNoisy({utteranceId}, noisy);
}

// ---- újraellenőrzés ---------------------------------------------------------

void TranscriptEditorViewModel::updateRecheckState()
{
    const bool can = m_editor && m_editor->canRecheck();
    const QString blocker = !m_editor ? QString() : can ? QString() : m_editor->recheckBlocker();
    if (can == m_canRecheck && blocker == m_recheckBlocker) return;
    m_canRecheck = can;
    m_recheckBlocker = blocker;
    emit recheckStateChanged();
}

QVariantMap TranscriptEditorViewModel::recheckSpeakers()
{
    QVariantMap out;
    if (!m_editor) {
        out[QStringLiteral("ran")] = false;
        return out;
    }
    if (!m_editor->canRecheck()) {
        out[QStringLiteral("ran")] = false;
        out[QStringLiteral("blocker")] = m_editor->recheckBlocker();
        return out;
    }
    // A szűrő-váltás és a jel a szerkesztő recheckFinished-jéből jön (a héj is azt váltja ki).
    const SpeakerEditor::RecheckResult r = m_editor->recheckFromConfirmed();
    out[QStringLiteral("ran")] = r.ran;
    out[QStringLiteral("flagged")] = r.flagged;
    out[QStringLiteral("speakersWithConfirmedCore")] = r.speakersWithConfirmedCore;
    out[QStringLiteral("confirmedLines")] = r.confirmedLines;
    out[QStringLiteral("referenceSummary")] = r.referenceSummary();
    return out;
}

// ---- másolás ----------------------------------------------------------------

QString TranscriptEditorViewModel::textOfUtterances(QVector<int> utterances) const
{
    std::sort(utterances.begin(), utterances.end());
    utterances.erase(std::unique(utterances.begin(), utterances.end()), utterances.end());
    if (utterances.size() == 1) {
        const EditorUtterance& u = m_utts[utterances.first()];
        return QStringLiteral("%1 (%2): %3").arg(m_views.value(u.speakerKey).name, timeLabel(u.startMs), u.text);
    }
    QString out;
    QString lastKey;
    int last = -2;
    for (int i : std::as_const(utterances)) {
        const EditorUtterance& u = m_utts[i];
        // Új fejsor beszélőváltáskor, vagy ha a kijelölés nem folytonos (kimaradt sorok).
        if (u.speakerKey != lastKey || i != last + 1) {
            if (!out.isEmpty()) out += QLatin1Char('\n');
            out += QStringLiteral("%1 (%2)\n").arg(m_views.value(u.speakerKey).name, timeLabel(u.startMs));
        }
        out += u.text + QLatin1Char('\n');
        lastKey = u.speakerKey;
        last = i;
    }
    return out;
}

QString TranscriptEditorViewModel::textOfRows(const QVariantList& rows) const
{
    QVector<int> utts;
    for (const QVariant& r : rows) {
        const int u = m_rows->utteranceOfRow(r.toInt());
        if (u >= 0) utts.append(u);
    }
    return utts.isEmpty() ? QString() : textOfUtterances(utts);
}

QString TranscriptEditorViewModel::selectionText() const
{
    if (m_selected.isEmpty()) return QString();
    return textOfUtterances(QVector<int>(m_selected.cbegin(), m_selected.cend()));
}

int TranscriptEditorViewModel::copyUtterances(const QVector<int>& utterances)
{
    if (utterances.isEmpty()) return 0;
    if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(textOfUtterances(utterances));
    emit notice(tr("%n megszólalás a vágólapra másolva.", nullptr, int(utterances.size())));
    return int(utterances.size());
}

int TranscriptEditorViewModel::copyRow(int row)
{
    const int u = m_rows->utteranceOfRow(row);
    return u >= 0 ? copyUtterances({u}) : 0;
}

int TranscriptEditorViewModel::copySelection()
{
    return copyUtterances(QVector<int>(m_selected.cbegin(), m_selected.cend()));
}

int TranscriptEditorViewModel::copyAll()
{
    QVector<int> all(m_utts.size());
    for (int i = 0; i < all.size(); ++i) all[i] = i;
    return copyUtterances(all);
}

void TranscriptEditorViewModel::selectAll()
{
    // A látható sorok (a „Bizonytalan" szűrőben csak a mutatottak).
    QSet<int> sel;
    for (const TranscriptListModel::Row& r : m_rows->rows())
        if (!r.gap) sel.insert(r.utterance);
    setSelection(sel, m_anchor >= 0 && sel.contains(m_anchor) ? m_anchor : -1);
}

// ---- „Következő bizonytalan" ------------------------------------------------

int TranscriptEditorViewModel::stepUncertain(int fromRow, int direction)
{
    const auto& rows = m_rows->rows();
    const int n = int(rows.size());
    if (n == 0) return -1;
    const int step = direction < 0 ? -1 : 1;
    int start = fromRow;
    if (start < 0 || start >= n) start = step > 0 ? -1 : n;
    for (int k = 1; k <= n; ++k) {
        const int r = ((start + step * k) % n + n) % n;
        if (rows[r].gap || !m_utts[rows[r].utterance].uncertain) continue;
        setSelection({rows[r].utterance}, rows[r].utterance);
        emit revealRequested(r);
        return r;
    }
    return -1;
}

// ---- „Meghallgatás" ---------------------------------------------------------

QVariantMap TranscriptEditorViewModel::speakerSample(const QString& speakerKey) const
{
    // A legjobb minta: nem bizonytalan, 4–15 mp közötti sorok közül a leghosszabb; ha nincs
    // ilyen, a leghosszabb sor (hosszú monológból az első ~12 mp).
    constexpr qint64 kMin = 4000, kMax = 15000, kCut = 12000;
    int best = -1;
    qint64 bestScore = -1;
    for (int i = 0; i < m_utts.size(); ++i) {
        const EditorUtterance& u = m_utts[i];
        if (u.speakerKey != speakerKey) continue;
        const qint64 len = u.endMs - u.startMs;
        if (len <= 0) continue;
        qint64 score = std::min(len, kMax);
        if (len >= kMin && len <= kMax) score += 100000;    // ideális hossz
        if (!u.uncertain) score += 50000;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best < 0) return {{QStringLiteral("ok"), false}};
    const EditorUtterance& u = m_utts[best];
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("startMs"), int(u.startMs)},
            {QStringLiteral("endMs"), int(std::min(u.endMs, u.startMs + kCut))}};
}

// ---- beszélő / hanglenyomat -------------------------------------------------

QVariantMap TranscriptEditorViewModel::speakerInfo(const QString& speakerKey) const
{
    // Ugyanaz a térkép, mint a `speakers` listában (benne a nameDuplicate jelzés is).
    for (const QVariant& v : std::as_const(m_speakerList))
        if (v.toMap().value(QStringLiteral("key")).toString() == speakerKey) return v.toMap();
    for (const EditorSpeaker& s : m_speakers)
        if (s.key == speakerKey) return speakerMap(s, m_views.value(s.key).lane);
    return {};
}

QVariantList TranscriptEditorViewModel::speakersMatching(const QString& query, const QString& excludeKey) const
{
    const QString needle = foldForSearch(query.trimmed());
    QVariantList out;
    for (const EditorSpeaker& s : m_speakers) {
        if (s.key == excludeKey) continue;
        // Névre VAGY becenévre: a megbeszélésen szereplő személyt a beceneve is megtalálja.
        QString matchedAlias;
        if (!needle.isEmpty() && !foldForSearch(s.displayName).contains(needle)) {
            if (m_editor && !s.anonymous)
                for (const QString& a : m_editor->personAliases(s.personName))
                    if (foldForSearch(a).contains(needle)) { matchedAlias = a; break; }
            if (matchedAlias.isEmpty()) continue;
        }
        QVariantMap entry = speakerInfo(s.key);
        entry.insert(QStringLiteral("matchedAlias"), matchedAlias);
        out.append(entry);
    }
    return out;
}

QString TranscriptEditorViewModel::speakerKeyForPerson(const QString& personName) const
{
    const QString name = personName.trimmed();
    for (const EditorSpeaker& s : m_speakers)
        if (!s.anonymous && s.personName.compare(name, Qt::CaseInsensitive) == 0) return s.key;
    return {};
}

QVariantMap TranscriptEditorViewModel::voiceprintMaterial(const QString& speakerKey) const
{
    QVariantMap out{{QStringLiteral("supported"), voiceAvailable()},
                    {QStringLiteral("reason"), QString()},
                    {QStringLiteral("usableLines"), 0},
                    {QStringLiteral("usableSec"), 0},
                    {QStringLiteral("missingSec"), 0},
                    {QStringLiteral("sufficient"), false}};
    if (!m_editor) return out;
    if (!m_editor->embeddingsSupported()) out[QStringLiteral("reason")] = QStringLiteral("model");
    else if (m_embeddingFailed) out[QStringLiteral("reason")] = QStringLiteral("audio");
    const VoiceprintMaterial m = m_editor->voiceprintMaterial(speakerKey);
    out[QStringLiteral("usableLines")] = m.usableLines;
    out[QStringLiteral("usableSec")] = int(m.usableMs / 1000);
    out[QStringLiteral("missingSec")] = int((m.missingMs + 999) / 1000);
    out[QStringLiteral("sufficient")] = m.sufficient;
    return out;
}

QVariantMap TranscriptEditorViewModel::createVoiceprint(const QString& speakerKey)
{
    QVariantMap out{{QStringLiteral("ok"), false}, {QStringLiteral("message"), QString()}};
    if (!m_editor) return out;
    const QString name = m_views.value(speakerKey).name;
    const VoiceprintResult r = m_editor->createVoiceprint(speakerKey);
    out[QStringLiteral("ok")] = r.ok;
    out[QStringLiteral("printId")] = r.printId;
    out[QStringLiteral("usedLines")] = r.usedLines;
    out[QStringLiteral("usedSec")] = int(r.usedMs / 1000);
    out[QStringLiteral("missingSec")] = int((r.missingMs + 999) / 1000);
    out[QStringLiteral("message")] = r.ok ? voiceprintMessage(name, r) : r.error;
    if (r.ok) {
        emit peopleChanged();
        emit notice(out.value(QStringLiteral("message")).toString());
    }
    return out;
}

QVariantMap TranscriptEditorViewModel::lineSampleInfo(const QString& utteranceId) const
{
    QVariantMap out{{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QString()},
                    {QStringLiteral("seconds"), 0}, {QStringLiteral("personName"), QString()}};
    const int i = m_uttIndex.value(utteranceId, -1);
    if (i < 0 || !m_editor) return out;
    const EditorUtterance& u = m_utts.at(i);
    const EditorSpeaker sp = m_editor->speaker(u.speakerKey);
    out[QStringLiteral("seconds")] = int((u.endMs - u.startMs) / 1000);
    out[QStringLiteral("personName")] = sp.personName;
    if (sp.anonymous)
        out[QStringLiteral("reason")] = tr("Előbb nevezd el a beszélőt.");
    else if (u.noisy)
        out[QStringLiteral("reason")] = tr("Egymásra beszéltek — nem tiszta minta.");
    else if (u.endMs - u.startMs < 3000)
        out[QStringLiteral("reason")] = tr("Túl rövid mintának (legalább 3 mp kell).");
    else if (!voiceAvailable())
        out[QStringLiteral("reason")] = tr("Nincs hangmodell vagy lekevert hang.");
    else
        out[QStringLiteral("ok")] = true;
    // Bizonytalan sor: a minta egyben megerősítés is („Jó így”), mert a felhasználó a sor
    // kiválasztásával dönt arról, hogy ez tényleg ennek a beszélőnek a hangja.
    out[QStringLiteral("confirmFirst")] = u.uncertain && out.value(QStringLiteral("ok")).toBool();
    return out;
}

QVariantMap TranscriptEditorViewModel::createVoiceprintFromLine(const QString& utteranceId)
{
    QVariantMap out{{QStringLiteral("ok"), false}, {QStringLiteral("message"), QString()}};
    const int i = m_uttIndex.value(utteranceId, -1);
    if (i < 0 || !m_editor) return out;
    const QString key = m_utts.at(i).speakerKey;
    const QString name = m_views.value(key).name;
    // Bizonytalan sorból: előbb megerősítjük (ugyanaz, mint a „Jó így”), aztán minta lesz belőle.
    if (m_utts.at(i).uncertain)
        m_editor->confirmUtterances({utteranceId}, /*asNoisy*/ false);
    const VoiceprintResult r = m_editor->createVoiceprintFromLines(key, {utteranceId});
    out[QStringLiteral("ok")] = r.ok;
    out[QStringLiteral("printId")] = r.printId;
    out[QStringLiteral("message")] = r.ok
        ? tr("Hanglenyomat-minta készült: %1 (ebből a sorból, %2 mp).").arg(name).arg(r.usedMs / 1000)
        : r.error;
    if (r.ok) {
        emit peopleChanged();
        emit notice(out.value(QStringLiteral("message")).toString());
    }
    return out;
}

QString TranscriptEditorViewModel::voiceprintMessage(const QString& name, const VoiceprintResult& r) const
{
    return tr("Hanglenyomat készült: %1 (%2 sorból, %3 mp beszédből).").arg(name).arg(r.usedLines).arg(r.usedMs / 1000);
}

bool TranscriptEditorViewModel::removeVoiceprint(const QString& printId)
{
    if (!m_editor || !m_editor->removeVoiceprint(printId)) return false;
    emit peopleChanged();
    return true;
}

// ---- személyek --------------------------------------------------------------

QVector<PersonInfo> TranscriptEditorViewModel::people() const
{
    if (m_peopleProvider) return m_peopleProvider();
    if (m_demoSession) return m_demoSession->people();
    if (m_controller) return m_controller->peopleDirectory();
    return {};
}

bool TranscriptEditorViewModel::isMeetingPerson(const QString& name) const
{
    for (const EditorSpeaker& s : m_speakers)
        if (!s.anonymous && s.personName.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

// ---- v3: bizonyíték, jelöltek, Átnézendő csoportok, új személy -------------------

namespace {

QString sideOf(Side side) { return sideName(side); }

// „a hívás hangján" / „a mikrofonon" — a mondatokba.
QString sidePhrase(const QString& side)
{
    if (side == QLatin1String("remote")) return TranscriptEditorViewModel::tr("a hívás hangján");
    if (side == QLatin1String("local")) return TranscriptEditorViewModel::tr("a mikrofonon");
    return {};
}

QString percentText(double v) { return QStringLiteral("%1%").arg(qRound(std::clamp(v, 0.0, 1.0) * 100.0)); }

} // namespace

bool TranscriptEditorViewModel::reviewRunning() const
{
    return m_editor && m_editor->isReviewRunning();
}

bool TranscriptEditorViewModel::isShort(int utterance) const
{
    if (utterance < 0 || utterance >= m_utts.size()) return false;
    const EditorUtterance& u = m_utts[utterance];
    return u.endMs - u.startMs < speakeredit::kMinEmbedMs;
}

QString TranscriptEditorViewModel::tagLabel(const QString& id) const
{
    if (m_controller && m_controller->tags()) {
        const QString name = m_controller->tags()->tag(id).name;
        if (!name.isEmpty()) return name;
    }
    if (m_demoSession) {
        const QString name = m_demoSession->tagName(id);
        if (!name.isEmpty()) return name;
    }
    return id;
}

QVariantMap TranscriptEditorViewModel::evidenceMap(const Evidence& e, const QString& sideContext, bool forLine) const
{
    QString kind;
    switch (e.kind) {
    case EvidenceKind::Voice: kind = QStringLiteral("voice"); break;
    case EvidenceKind::Side:
    case EvidenceKind::Manual: kind = QStringLiteral("side"); break;
    case EvidenceKind::Tag: kind = QStringLiteral("tag"); break;
    case EvidenceKind::LineCount: kind = QStringLiteral("lines"); break;
    case EvidenceKind::Similarity: kind = QStringLiteral("similarity"); break;
    case EvidenceKind::Calendar: kind = QStringLiteral("calendar"); break;
    }
    const QString polarity = ranking::polarityName(e.polarity);
    QString side;
    if (kind == QLatin1String("side")) {
        QString s = sideContext;
        if (s.isEmpty()) {
            if (e.text.contains(tr("hívás"))) s = QStringLiteral("remote");
            else if (e.text.contains(tr("mikrofon"))) s = QStringLiteral("local");
        }
        side = s == QLatin1String("remote") ? QStringLiteral("loopback") : s == QLatin1String("local") ? QStringLiteral("mic")
                                                                                                         : QString();
    }

    QString text = e.text, label, sentence;
    switch (e.kind) {
    case EvidenceKind::Voice:
        label = tr("Hang:");
        sentence = e.value > 0.0 && !e.detail.isEmpty() ? tr("%1, %2").arg(percentText(e.value), e.detail) : e.text;
        break;
    case EvidenceKind::Side:
    case EvidenceKind::Manual:
        label = tr("Sáv:");
        if (e.polarity == Polarity::Support) {
            if (forLine) text = tr("ugyanaz a sáv");
            sentence = e.text.endsWith(tr("beszél")) ? e.text : tr("%1 beszél").arg(e.text);
            if (e.kind == EvidenceKind::Manual) sentence += tr(" (kézi sáv-beosztás)");
        } else {
            sentence = e.text;
        }
        break;
    case EvidenceKind::Tag: {
        label = tr("Címke:");
        if (e.polarity == Polarity::Support) {
            QStringList names;
            for (const QString& id : e.detail.split(QStringLiteral(", "), Qt::SkipEmptyParts))
                names << QStringLiteral("#") + tagLabel(id.trimmed());
            if (!names.isEmpty()) text = names.join(QStringLiteral(" "));
            sentence = tr("közös címke a megbeszéléssel: %1").arg(names.join(QStringLiteral(", ")));
        } else {
            sentence = tr("nincs közös címkéje a megbeszéléssel");
        }
        break;
    }
    case EvidenceKind::Similarity: {
        label = tr("Figyelem:");
        const QString other = e.fixTarget.startsWith(QLatin1String("pair:"))
            ? m_views.value(e.fixTarget.mid(5)).name : QString();
        sentence = other.isEmpty() ? e.text
                                   : tr("%1 hangja nagyon hasonló (%2)").arg(other, QLocale(QLocale::Hungarian).toString(e.value, 'f', 2));
        break;
    }
    case EvidenceKind::LineCount:
        label = tr("Sorok:");
        sentence = e.text;
        break;
    case EvidenceKind::Calendar:
        label = tr("Naptár:");
        sentence = e.detail.isEmpty() ? e.text : e.detail;
        break;
    }

    QString fixLabel;
    if (e.fixTarget == QLatin1String("tracks")) fixLabel = forLine ? tr("Sáv-beosztás") : tr("Módosítás");
    else if (e.fixTarget == QLatin1String("samples")) fixLabel = tr("Minták");
    else if (e.fixTarget == QLatin1String("tags")) fixLabel = tr("Címkéi");
    else if (e.fixTarget.startsWith(QLatin1String("pair:"))) fixLabel = tr("Kettejük átnézése");

    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("manual"), e.kind == EvidenceKind::Manual},
        {QStringLiteral("polarity"), polarity},
        {QStringLiteral("side"), side},
        {QStringLiteral("text"), text},
        {QStringLiteral("label"), label},
        {QStringLiteral("sentence"), sentence},
        {QStringLiteral("detail"), e.detail},
        {QStringLiteral("fixTarget"), e.fixTarget},
        {QStringLiteral("fixLabel"), fixLabel},
        {QStringLiteral("pairKey"), e.fixTarget.startsWith(QLatin1String("pair:")) ? e.fixTarget.mid(5) : QString()},
        {QStringLiteral("value"), e.value},
    };
}

QVariantList TranscriptEditorViewModel::evidenceList(const QVector<Evidence>& list, const QString& sideContext,
                                                     bool forLine, bool chipsOnly) const
{
    QVariantList out;
    for (const Evidence& e : list) {
        if (chipsOnly && e.kind == EvidenceKind::LineCount) continue;
        out.append(evidenceMap(e, sideContext, forLine));
    }
    return out;
}

QString TranscriptEditorViewModel::speakerSideName(const QString& speakerKey) const
{
    return m_speakerSides.value(speakerKey, QStringLiteral("unknown"));
}

QString TranscriptEditorViewModel::lineSideName(const QString& utteranceId) const
{
    if (!m_editor) return QStringLiteral("unknown");
    const SideReport r = m_editor->sideReport();
    if (!r.active) return QStringLiteral("unknown");
    for (const LineSide& l : r.lines)
        if (l.utteranceId == utteranceId) return sideOf(l.side);
    return QStringLiteral("unknown");
}

QVector<Candidate> TranscriptEditorViewModel::candidatesForLine(const QString& utteranceId) const
{
    return m_editor ? m_editor->lineCandidates(utteranceId) : QVector<Candidate>();
}

QVector<Candidate> TranscriptEditorViewModel::candidatesForSpeaker(const QString& speakerKey) const
{
    return m_editor ? m_editor->speakerCandidates(speakerKey) : QVector<Candidate>();
}

QVariantList TranscriptEditorViewModel::whyNot(const QString& utteranceId) const
{
    const int i = m_uttIndex.value(utteranceId, -1);
    if (!m_editor || i < 0) return {};
    const QString speakerKey = m_utts[i].speakerKey;
    const QString lineSide = lineSideName(utteranceId);
    const QString speakerSide = speakerSideName(speakerKey);
    // A rövid név („Lilla") a mondatban: elnevezett személynél az utónév.
    QString shortName = m_views.value(speakerKey).name;
    if (!m_editor->speaker(speakerKey).anonymous && shortName.contains(QLatin1Char(' ')))
        shortName = shortName.section(QLatin1Char(' '), -1);
    QVariantList out;
    for (const Evidence& e : m_editor->whyNot(utteranceId)) {
        QVariantMap m = evidenceMap(e, speakerSide, /*forLine*/ true);
        if ((e.kind == EvidenceKind::Side || e.kind == EvidenceKind::Manual) && e.polarity == Polarity::Contradict
            && !sidePhrase(lineSide).isEmpty() && !sidePhrase(speakerSide).isEmpty()) {
            m[QStringLiteral("sentence")] = tr("a sor %1 jött, %2 %3 beszél.")
                                                .arg(sidePhrase(lineSide), shortName, sidePhrase(speakerSide));
        }
        out.append(m);
    }
    return out;
}

QVariantList TranscriptEditorViewModel::speakerEvidence(const QString& speakerKey) const
{
    if (!m_editor) return {};
    return evidenceList(m_editor->speakerEvidence(speakerKey), speakerSideName(speakerKey), false, /*chipsOnly*/ true);
}

QVariantMap TranscriptEditorViewModel::lineInfo(const QString& utteranceId) const
{
    const int i = m_uttIndex.value(utteranceId, -1);
    if (i < 0) return {};
    const EditorUtterance& u = m_utts[i];
    return {{QStringLiteral("utteranceId"), u.id},
            {QStringLiteral("speakerKey"), u.speakerKey},
            {QStringLiteral("timeLabel"), timeLabel(u.startMs)},
            {QStringLiteral("startMs"), int(u.startMs)},
            {QStringLiteral("endMs"), int(u.endMs)}};
}

QVariantList TranscriptEditorViewModel::trackOptions(const QString& speakerKey) const
{
    QVector<Track> tracks;
    if (m_demoSession) tracks = m_demoSession->tracks();
    else if (m_controller && !m_meetingId.isEmpty()) tracks = m_controller->store()->load(m_meetingId).tracks;
    const QStringList assigned = m_editor ? m_editor->trackAssignment(speakerKey) : QStringList();
    int mics = 0, loops = 0;
    for (const Track& t : std::as_const(tracks)) {
        if (!t.active) continue;
        mics += t.kind == TrackKind::Mic;
        loops += t.kind == TrackKind::Loopback;
    }
    // Egyoldalú meeting (csak mikrofon vagy csak hívás): nincs mit beosztani.
    if (mics == 0 || loops == 0) return {};
    QVariantList out;
    for (const Track& t : std::as_const(tracks)) {
        if (!t.active) continue;
        const bool mic = t.kind == TrackKind::Mic;
        const bool loop = t.kind == TrackKind::Loopback;
        QString name = t.customName;
        if (name.isEmpty()) {
            if (mic && mics == 1) name = tr("Saját mikrofon");
            else if (loop && loops == 1) name = tr("Hívás hangja");
            else name = t.deviceName.isEmpty() ? t.id : t.deviceName;
        }
        out.append(QVariantMap{{QStringLiteral("id"), t.id},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("kind"), mic ? QStringLiteral("mic") : loop ? QStringLiteral("loopback")
                                                                                           : QStringLiteral("other")},
                               {QStringLiteral("checked"), assigned.contains(t.id)}});
    }
    return out;
}

bool TranscriptEditorViewModel::setSpeakerTracks(const QString& speakerKey, const QStringList& trackIds)
{
    if (!m_editor) return false;
    const bool ok = m_editor->setTrackAssignment(speakerKey, trackIds);
    if (ok) emit reviewChanged();
    return ok;
}

QString TranscriptEditorViewModel::sideBasisText(const QString& speakerKey) const
{
    if (!m_editor) return {};
    const SideReport r = m_editor->sideReport();
    for (const PersonSide& p : r.persons) {
        if (p.speakerKey != speakerKey) continue;
        if (p.basis == QLatin1String("manual"))
            return tr("kézi beosztás: %1").arg(sidePhrase(sideOf(p.manualSide)));
        if (p.learnedSide == Side::Local || p.learnedSide == Side::Remote)
            return tr("tanult: jellemzően %1").arg(sidePhrase(sideOf(p.learnedSide)));
        if (p.basis == QLatin1String("lines") && (p.side == Side::Local || p.side == Side::Remote))
            return tr("a megerősített sorai alapján: %1").arg(sidePhrase(sideOf(p.side)));
        if (p.basis == QLatin1String("user-name"))
            return tr("te vagy: a saját mikrofonodon");
        return {};
    }
    return {};
}

QString TranscriptEditorViewModel::personOf(const QString& speakerKey) const
{
    return m_editor ? m_editor->speaker(speakerKey).personName : QString();
}

void TranscriptEditorViewModel::scheduleReview()
{
    if (!m_reviewTimer.isActive()) m_reviewTimer.start();
}

void TranscriptEditorViewModel::rebuildReview()
{
    m_reviewTimer.stop();
    m_reviewViews.clear();
    m_contaminated.clear();
    m_newPersonGroupId.clear();
    QSet<QString> sideConflicts;
    QHash<QString, QString> sides;
    if (m_editor && !m_utts.isEmpty()) {
        const SideReport sr = m_editor->sideReport();
        if (sr.active) {
            for (const SideConflict& c : sr.conflicts) sideConflicts.insert(c.utteranceId);
            for (const PersonSide& p : sr.persons) sides.insert(p.speakerKey, sideOf(p.side));
        }
        QSet<QString> covered;
        QVector<ReviewView> shortViews;
        for (const ReviewGroup& g : m_editor->reviewGroups()) {
            for (const QString& id : g.utteranceIds) covered.insert(id);
            if (m_skippedGroups.contains(g.id)) continue;
            if (g.kind == ReviewKind::ContaminatedCore) {
                if (!m_contaminated.isEmpty()) continue;
                const EditorSpeaker sp = m_editor->speaker(g.currentSpeakerKey);
                m_contaminated = {{QStringLiteral("groupId"), g.id},
                                  {QStringLiteral("speakerKey"), g.currentSpeakerKey},
                                  {QStringLiteral("name"), sp.displayName},
                                  {QStringLiteral("title"), tr("%1 sorai két különböző hangnak tűnnek").arg(sp.displayName)},
                                  {QStringLiteral("detail"), g.subtitle},
                                  {QStringLiteral("count"), int(g.utteranceIds.size())},
                                  {QStringLiteral("confirmed"), g.confirmedBasis},
                                  {QStringLiteral("lines"), sp.utteranceCount},
                                  {QStringLiteral("evidence"), evidenceList(g.evidence, QString(), false, true)}};
                continue;
            }
            ReviewView v;
            v.id = g.id;
            v.kind = reviewKindName(g.kind);
            v.title = g.title;
            v.subtitle = g.subtitle;
            v.ids = g.utteranceIds;
            v.currentKey = g.currentSpeakerKey;
            v.proposedKey = g.proposedSpeakerKey;
            v.proposedName = g.proposedPersonName;
            const QString ctx = sides.value(!g.proposedSpeakerKey.isEmpty() ? g.proposedSpeakerKey : g.currentSpeakerKey);
            v.evidence = evidenceList(g.evidence, ctx, g.kind == ReviewKind::SideConflict, true);
            v.actionable = g.kind != ReviewKind::ShortLines;
            if (g.kind == ReviewKind::ShortLines) {
                v.subtitle = tr("%1 mp alattiak, csak fülre dönthetők; nem számítanak az átnézendők közé.")
                                 .arg(QLocale(QLocale::Hungarian).toString(speakeredit::kMinEmbedMs / 1000.0, 'f', 1));
                shortViews.append(v);
                continue;
            }
            if (g.kind == ReviewKind::SimilarToNewPerson && !m_newPersonKey.isEmpty()
                && g.proposedSpeakerKey == m_newPersonKey)
                m_newPersonGroupId = g.id;
            m_reviewViews.append(v);
        }
        // A csoportokon kívüli, hangra kétes sorok (a régi „Bizonytalan" szűrő sorai): a javasolt
        // beszélő szerint csoportosítva; javaslat nélkül egy közös, csak „Egyenként" csoport.
        QMap<QString, QStringList> byPair;      // "jelenlegi\x1fjavasolt" → sorok
        QStringList loose;
        for (const EditorUtterance& u : std::as_const(m_utts)) {
            if (!u.uncertain || u.confirmed || covered.contains(u.id)) continue;
            if (!u.likelySpeakerKey.isEmpty() && m_views.contains(u.likelySpeakerKey))
                byPair[u.speakerKey + QChar(0x1f) + u.likelySpeakerKey] << u.id;
            else
                loose << u.id;
        }
        for (auto it = byPair.cbegin(); it != byPair.cend(); ++it) {
            const QString cur = it.key().section(QChar(0x1f), 0, 0);
            const QString likely = it.key().section(QChar(0x1f), 1, 1);
            ReviewView v;
            v.id = QStringLiteral("u:%1:%2").arg(cur, likely);
            if (m_skippedGroups.contains(v.id)) continue;
            v.kind = QStringLiteral("uncertain");
            v.ids = it.value();
            v.currentKey = cur;
            v.proposedKey = likely;
            v.title = tr("%n sor hangja inkább: %1", nullptr, int(v.ids.size())).arg(m_views.value(likely).name);
            v.subtitle = tr("Most: %1").arg(m_views.value(cur).name);
            m_reviewViews.append(v);
        }
        if (!loose.isEmpty() && !m_skippedGroups.contains(QStringLiteral("u:"))) {
            ReviewView v;
            v.id = QStringLiteral("u:");
            v.kind = QStringLiteral("uncertain");
            v.ids = loose;
            v.title = tr("%n hangra kétes sor", nullptr, int(loose.size()));
            v.subtitle = tr("A hang alapján nem dönthető el biztosan, kié — egyenként érdemes meghallgatni.");
            m_reviewViews.append(v);
        }
        m_reviewViews += shortViews;
    }
    int count = 0, groups = 0;
    for (const ReviewView& v : std::as_const(m_reviewViews)) {
        if (!v.actionable) continue;
        count += int(v.ids.size());
        ++groups;
    }
    m_reviewCount = count;
    m_reviewGroupCount = groups;

    // A sín-fejléc oldal-ikonjai; a sorok sáv-ellentmondás jelzése.
    const bool sidesChanged = sides != m_speakerSides;
    const bool conflictsChanged = sideConflicts != m_sideConflicts;
    m_speakerSides = sides;
    m_sideConflicts = sideConflicts;
    if (sidesChanged) {
        rebuildSpeakers(false);
        emit speakersChanged();
    }
    if (conflictsChanged) m_rows->notifyAll({TranscriptListModel::SideConflictRole});
    m_review->sync();
    updateSimilar();
    emit reviewChanged();
}

void TranscriptEditorViewModel::updateSimilar()
{
    QSet<int> similar;
    for (const ReviewView& v : std::as_const(m_reviewViews)) {
        if (v.id != m_newPersonGroupId) continue;
        for (const QString& id : v.ids) {
            const int i = m_uttIndex.value(id, -1);
            if (i >= 0) similar.insert(i);
        }
    }
    // Az új személy hasonló sorai először maguktól látszanak („Megmutatva a sínen").
    if (!similar.isEmpty() && !m_similarAutoShown) {
        m_similarAutoShown = true;
        m_similarShown = true;
    }
    if (similar == m_similar && !m_similarMarks.isEmpty() == (m_similarShown && !similar.isEmpty())) return;
    QVector<int> touched;
    for (int i : std::as_const(m_similar)) touched.append(i);
    for (int i : std::as_const(similar)) touched.append(i);
    m_similar = similar;
    m_similarMarks.clear();
    if (m_similarShown) {
        const double total = double(std::max<qint64>({m_durationMs, m_timelineMs, 1}));
        QVector<int> sorted(m_similar.cbegin(), m_similar.cend());
        std::sort(sorted.begin(), sorted.end());
        for (int i : std::as_const(sorted)) {
            m_similarMarks << qreal(m_utts[i].startMs) / total
                           << qreal(std::max<qint64>(0, m_utts[i].endMs - m_utts[i].startMs)) / total;
        }
    }
    m_rows->notifyUtterances(touched, {TranscriptListModel::SuggestedRole});
}

void TranscriptEditorViewModel::setSimilarShown(bool shown)
{
    if (m_similarShown == shown) return;
    m_similarShown = shown;
    m_similarAutoShown = true;
    m_similarMarks.clear();
    QSet<int> keep = m_similar;
    m_similar.clear();          // updateSimilar újraszámolja (és értesít)
    for (int i : std::as_const(keep)) m_rows->notifyUtterances({i}, {TranscriptListModel::SuggestedRole});
    updateSimilar();
    if (shown) {
        setRailVisible(true);
        int first = -1;
        for (int i : std::as_const(m_similar)) first = first < 0 ? i : std::min(first, i);
        const int row = m_rows->nearestRow(first);
        if (row >= 0) emit revealRequested(row);
    }
    emit reviewChanged();
}

QVariantMap TranscriptEditorViewModel::newPersonSimilar() const
{
    for (const ReviewView& v : m_reviewViews) {
        if (v.id != m_newPersonGroupId || v.ids.isEmpty()) continue;
        QStringList parts;
        for (const QVariant& e : v.evidence) parts << e.toMap().value(QStringLiteral("text")).toString();
        return {{QStringLiteral("groupId"), v.id},
                {QStringLiteral("count"), int(v.ids.size())},
                {QStringLiteral("name"), m_views.value(m_newPersonKey).name},
                {QStringLiteral("detail"), parts.join(QStringLiteral(", "))}};
    }
    return {};
}

QVariantMap TranscriptEditorViewModel::newPersonReadiness() const
{
    if (!m_editor || m_newPersonKey.isEmpty() || !voiceAvailable()) return {};
    const EditorSpeaker sp = m_editor->speaker(m_newPersonKey);
    if (sp.key.isEmpty() || sp.anonymous || sp.hasVoiceprint) return {};
    const VoiceprintMaterial m = m_editor->voiceprintMaterial(m_newPersonKey);
    const int now = int(m.usableMs / 1000);
    const int missing = int((m.missingMs + 999) / 1000);
    const bool pending = m_voiceprintWhenReady == m_newPersonKey;
    return {{QStringLiteral("ready"), m.sufficient},
            {QStringLiteral("pending"), pending},
            {QStringLiteral("text"), m.sufficient ? tr("Elég tiszta beszéd van a hanglenyomathoz (%1 mp).").arg(now)
                                                  : tr("Hanglenyomathoz még kb. %1 mp tiszta beszéd kell (most %2 mp).")
                                                        .arg(missing).arg(now)},
            {QStringLiteral("action"), m.sufficient ? tr("Hanglenyomat készítése")
                                       : pending   ? tr("Elkészül, ha elég lesz")
                                                   : tr("Hanglenyomat, ha elég")}};
}

QVariantMap TranscriptEditorViewModel::requestNewPersonVoiceprint()
{
    if (!m_editor || m_newPersonKey.isEmpty()) return {{QStringLiteral("ok"), false}};
    if (m_editor->voiceprintMaterial(m_newPersonKey).sufficient) {
        QVariantMap r = createVoiceprint(m_newPersonKey);
        emit reviewChanged();
        return r;
    }
    m_voiceprintWhenReady = m_newPersonKey;
    emit reviewChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("pending"), true}};
}

bool TranscriptEditorViewModel::applyReviewGroup(const QString& groupId)
{
    if (!m_editor) return false;
    const auto it = std::find_if(m_reviewViews.cbegin(), m_reviewViews.cend(),
                                 [&](const ReviewView& v) { return v.id == groupId; });
    if (it == m_reviewViews.cend() || !it->actionable) return false;
    const ReviewView v = *it;
    if (v.proposedKey.isEmpty() && v.proposedName.isEmpty()) return false;
    int moved = 0, last = -1;
    for (const QString& id : v.ids) {
        const int i = m_uttIndex.value(id, -1);
        if (i < 0 || (!v.proposedKey.isEmpty() && m_utts[i].speakerKey == v.proposedKey)) continue;
        ++moved;
        last = std::max(last, i);
    }
    const QList<QString> keysBefore = m_views.keys();
    ++m_opDepth;
    const bool ok = v.kind == QLatin1String("uncertain") ? m_editor->moveUtterances(v.ids, v.proposedKey)
                                                         : m_editor->applyReviewGroup(groupId);
    --m_opDepth;
    if (!ok) return false;
    QString target = !v.proposedKey.isEmpty() && m_views.contains(v.proposedKey) ? v.proposedKey
                                                                                   : speakerKeyForPerson(v.proposedName);
    if (target.isEmpty())
        for (const QString& k : m_views.keys())
            if (!keysBefore.contains(k)) target = k;
    afterMove(target);
    const bool toNew = !m_newPersonKey.isEmpty() && target == m_newPersonKey;
    publishChange(tr("%n sor átkerült ide: %1", nullptr, moved).arg(m_views.value(target).name), QString(),
                  target, last, false, toNew);
    rebuildReview();
    return true;
}

void TranscriptEditorViewModel::skipReviewGroup(const QString& groupId)
{
    m_skippedGroups.insert(groupId);
    rebuildReview();
}

bool TranscriptEditorViewModel::splitSpeaker(const QString& speakerKey)
{
    if (!m_editor) return false;
    const int count = m_contaminated.value(QStringLiteral("count")).toInt();
    const QList<QString> keysBefore = m_views.keys();
    // A cél: a szennyezett-mag csoport javaslata (a core ugyanazt számolja újra).
    QString proposed;
    for (const ReviewGroup& g : m_editor->reviewGroups())
        if (g.kind == ReviewKind::ContaminatedCore && g.currentSpeakerKey == speakerKey)
            proposed = !g.proposedSpeakerKey.isEmpty() ? g.proposedSpeakerKey : speakerKeyForPerson(g.proposedPersonName);
    ++m_opDepth;
    const bool ok = m_editor->splitSpeaker(speakerKey);
    --m_opDepth;
    if (!ok) return false;
    QString target = proposed;
    if (target.isEmpty())
        for (const QString& k : m_views.keys())
            if (!keysBefore.contains(k)) target = k;
    afterMove(target);
    publishChange(tr("Szétválasztás: %n sor átkerült ide: %1", nullptr, count).arg(m_views.value(target).name),
                  QString(), target, -1);
    return true;
}

bool TranscriptEditorViewModel::confirmUtterance(const QString& utteranceId, bool asNoisy)
{
    return m_editor && m_uttIndex.contains(utteranceId) && m_editor->confirmUtterances({utteranceId}, asNoisy);
}

bool TranscriptEditorViewModel::acceptNewPersonSimilar()
{
    return !m_newPersonGroupId.isEmpty() && applyReviewGroup(m_newPersonGroupId);
}

// ---- demó-állapotok ---------------------------------------------------------

void TranscriptEditorViewModel::applyDemoState(const QString& state)
{
    m_pendingDemoState = state;
    if (m_editor && m_editor->isEmbeddingRunning()) return;    // a hang-elemzés végén
    applyPendingDemoState();
}

void TranscriptEditorViewModel::applyPendingDemoState()
{
    const QString state = m_pendingDemoState;
    m_pendingDemoState.clear();
    if (state.isEmpty() || !m_editor || m_utts.isEmpty()) return;

    if (state == QLatin1String("selection")) {
        setRailVisible(true);
        QSet<int> sel;
        for (int i = 2; i <= 4 && i < m_utts.size(); ++i) sel.insert(i);
        setSelection(sel, 2);
    } else if (state == QLatin1String("suggestion") || state == QLatin1String("suggestionShown")) {
        setRailVisible(true);
        if (m_demoSession && !m_demoSession->suggestionSeedUtteranceId().isEmpty()) {
            moveUtteranceToSpeaker(m_demoSession->suggestionSeedUtteranceId(),
                                   m_demoSession->suggestionTargetKey());
            if (state == QLatin1String("suggestionShown")) setSuggestionShown(true);
            const int row = m_rows->nearestRow(m_suggestionAnchor);
            if (row >= 0) emit revealRequested(row);
        }
    } else if (state == QLatin1String("filter")) {
        setRailVisible(true);
        setUncertainOnly(true);
    } else if (state == QLatin1String("search")) {
        setSearchQuery(QStringLiteral("sugo"));
    } else if (state == QLatin1String("searchEmpty")) {
        setSearchQuery(QStringLiteral("zsiráf"));
    } else if (state == QLatin1String("rail")) {
        setRailVisible(true);
    } else if (state == QLatin1String("changeLine")) {
        // Egy sor átkerül a szomszéd oszlopba: nincs hasonló-sor javaslat, csak a „mind a N
        // sora" folytatás.
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
        reassignSpeaker(m_laneKeys.value(0), QStringLiteral("Molnár Eszter"), false);
    } else if (state == QLatin1String("changeVoiceprint") || state == QLatin1String("changeVoiceprintDone")) {
        // Egy névtelen beszélő nevet kap (a személynek nincs lenyomata): a sáv felajánlja.
        setRailVisible(true);
        for (const EditorSpeaker& s : std::as_const(m_speakers)) {
            if (!s.anonymous || m_views.value(s.key).lane < 0) continue;
            reassignSpeaker(s.key, QStringLiteral("Bálint Péter"), false);
            break;
        }
        if (state == QLatin1String("changeVoiceprintDone")) createVoiceprintFromChange();
    } else if (state == QLatin1String("voiceprintNone") || state == QLatin1String("voiceprintDone")) {
        // Legyen elnevezett, lenyomat nélküli beszélő (a panelt a TranscriptTab nyitja).
        bool exists = false;
        for (const EditorSpeaker& s : std::as_const(m_speakers)) exists = exists || (!s.anonymous && !s.hasVoiceprint);
        for (const EditorSpeaker& s : std::as_const(m_speakers)) {
            if (exists || !s.anonymous) continue;
            reassignSpeaker(s.key, QStringLiteral("Bálint Péter"), false);
            break;
        }
        dismissChange();
    } else if (state == QLatin1String("voiceprintShort")) {
        // Egyetlen rövid sor egy új személynél: ebből nem készíthető lenyomat.
        if (m_utts.size() > 3) moveUtteranceToPerson(m_utts[3].id, QStringLiteral("Bálint Péter"));
        dismissChange();
    } else if (state == QLatin1String("recheck") || state == QLatin1String("recheckReady")) {
        // Beszélőnként 3 biztos sor megerősítve. recheckReady: a mostani bizonytalanok is
        // eldöntve („Jó így") → a szűrő-gomb az újraellenőrzést kínálja; recheck: le is fut.
        setRailVisible(true);
        const bool ready = state == QLatin1String("recheckReady");
        QStringList ids = ready ? m_editor->uncertainUtteranceIds() : QStringList();
        QHash<QString, int> perSpeaker;
        for (const EditorUtterance& u : std::as_const(m_utts)) {
            if (u.uncertain || u.endMs - u.startMs < 3000) continue;
            if (perSpeaker.value(u.speakerKey) >= 3) continue;
            ++perSpeaker[u.speakerKey];
            ids << u.id;
        }
        m_editor->confirmUtterances(ids);
        if (!ready) recheckSpeakers();
    } else if (state == QLatin1String("noisy")) {
        // Egy hosszabb sor „Jó így, de ne használd mintának" jelzést kap.
        for (const EditorUtterance& u : std::as_const(m_utts)) {
            if (u.uncertain || u.noisy || u.endMs - u.startMs < 3000) continue;
            m_editor->confirmUtterances({u.id}, /*asNoisy*/ true);
            const int row = m_rows->rowOfUtterance(m_uttIndex.value(u.id, -1));
            if (row >= 0) emit revealRequested(row);
            break;
        }
    } else if (state == QLatin1String("changePairOffer")) {
        // Egy sor átkerül egy másik elnevezett beszélőhöz; a sáv a kettejük átnézését ajánlja
        // (a kitalált hangok ehhez túl különbözők, ezért az ajánlat itt kitalált).
        setRailVisible(true);
        QString from, to;
        int moved = -1;
        for (int i = 3; i < m_utts.size() && moved < 0; ++i) {
            if (m_editor->speaker(m_utts[i].speakerKey).anonymous) continue;
            for (const QString& key : std::as_const(m_laneKeys)) {
                if (key == m_utts[i].speakerKey || m_editor->speaker(key).anonymous) continue;
                from = m_utts[i].speakerKey;
                to = key;
                moved = i;
                break;
            }
        }
        if (moved >= 0 && moveUtteranceToSpeaker(m_utts[moved].id, to)) {
            m_editor->dismissSuggestion();      // a kettő együtt sosem jelenik meg
            m_demoPairOffer = true;
            m_pairOffer.sourceSpeakerKey = from;
            m_pairOffer.targetSpeakerKey = to;
            m_pairOffer.centroidSimilarity = 0.82;
            emit suggestionChanged();
        }
    } else if (state == QLatin1String("markers") || state == QLatin1String("fixLinePopover")
               || state == QLatin1String("reviewGroups") || state == QLatin1String("reviewExpanded")
               || state == QLatin1String("contaminatedCore") || state == QLatin1String("speakerWhy")
               || state == QLatin1String("newPersonSimilar")) {
        // v3 Javítás mód (a „v3" kitalált meetingen): megerősített magok, jelölők; a panelt /
        // a csoport kinyitását a TranscriptTab végzi.
        setRailVisible(true);
        if (m_demoSession) m_demoSession->stageV3(state);
        if (state.startsWith(QLatin1String("review")) || state == QLatin1String("contaminatedCore"))
            setUncertainOnly(true);
        if (state == QLatin1String("newPersonSimilar") && m_utts.size() > 8) {
            setSelection({6, 7, 8}, 6);
            moveSelectionToPerson(QStringLiteral("Nagy Péter"));
            const int row = m_rows->nearestRow(6);
            if (row >= 0) emit revealRequested(row);
        }
        if (state == QLatin1String("markers")) clearChange();
    } else if (state == QLatin1String("changeFilter")) {
        // A szűrőben javított sor a helyén marad („javítva"), alatta a sáv.
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

} // namespace tanara_qml
