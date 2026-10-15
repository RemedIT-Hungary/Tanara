#include "PreTranscriptViewModel.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "TagMatching.h"

#include "tanara/processing/ITranscribeStepFacade.h"
#include "tanara/providers/stt/WhisperPrompt.h"

namespace tanara_qml {

using namespace tanara;

namespace {

QVariantMap stageMap(const QString& id, const QString& label, const QString& state,
                     int percent = -1, const QString& detail = QString())
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("state"), state}, {QStringLiteral("percent"), percent},
            {QStringLiteral("detail"), detail}};
}

} // namespace

PreTranscriptViewModel::PreTranscriptViewModel(QObject* parent)
    : QObject(parent), m_note(new MeetingNoteModel(this)), m_tags(new MeetingTagsModel(this))
{
    init();
    // Without given services the model follows the App singleton's.
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { resolveFacade(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this]() { resolveFacade(); reload(); });
    resolveFacade();
    reload();
}

PreTranscriptViewModel::PreTranscriptViewModel(const UiServices& services, QObject* parent)
    : QObject(parent), m_note(new MeetingNoteModel(this)), m_tags(new MeetingTagsModel(services, this)),
      m_services(&services)
{
    init();
    resolveFacade();
    reload();
}

void PreTranscriptViewModel::init()
{
    // The tag suggestion row follows the field's tags, the regular suggestions and undo.
    connect(m_tags, &MeetingTagsModel::tagsChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::suggestionsChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::undoChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::toast, this, &PreTranscriptViewModel::toast);
    connect(m_note, &MeetingNoteModel::noteChanged, this, [this]() {
        recomputeContextBudget();
        emit contextNoteChanged();
        emit changed();   // the run view's footer quotes from this too
    });
    // The remaining time ticks down by the wall clock: recomputed periodically while running.
    m_etaTimer.setInterval(10000);
    connect(&m_etaTimer, &QTimer::timeout, this, &PreTranscriptViewModel::jobChanged);
}

void PreTranscriptViewModel::resolveFacade()
{
    const UiServices& set = m_services ? *m_services : AppContext::instance()->services();
    // The real facade is shared; a fictional one serves this model's design state and is its child.
    if (m_facade && m_facadeSet == &set)
        return;
    m_facadeSet = &set;
    attachFacade(set.transcribeStep ? set.transcribeStep(m_demoState, this) : nullptr);
}

void PreTranscriptViewModel::attachFacade(processing::ITranscribeStepFacade* facade)
{
    if (facade == m_facade)
        return;
    if (m_facade) {
        m_facade->disconnect(this);
        if (m_facade->parent() == this)
            delete m_facade.data();
    }
    m_facade = facade;
    m_note->setController(facade ? facade->noteController() : nullptr);
    if (!facade)
        return;
    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    connect(facade, &processing::ITranscribeStepFacade::changed, this,
            [this, mine](const QString& id) { if (mine(id)) reload(); });
    connect(facade, &processing::ITranscribeStepFacade::settingsChanged, this, &PreTranscriptViewModel::reload);
    // Progress: only the phase list refreshes (no disk read).
    connect(facade, &processing::ITranscribeStepFacade::transcribeProgress, this,
            [this, mine](const QString& id, const JobProgress& p) {
        if (!mine(id) || m_state != QLatin1String("running"))
            return;
        applyJob(p);
        emit jobChanged();
    });
    connect(facade, &processing::ITranscribeStepFacade::mixdownProgress, this,
            [this, mine](const QString& id, int pct) {
        if (!mine(id) || pct == m_mixdownPercent) return;
        m_mixdownPercent = pct;
        emit mixdownPercentChanged();
    });
}

bool PreTranscriptViewModel::live() const { return m_facade && m_facade->live(); }

bool PreTranscriptViewModel::demo() const { return !live(); }

void PreTranscriptViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    // The not-yet-saved note belongs to the OLD meeting: it is written there before the switch.
    m_note->setMeetingId(id);
    m_meetingId = id;
    // In the demo an empty id -> the fictional demo meeting's tags.
    m_tags->setMeetingId(live() ? id : QString());
    reload();                 // new data first, so the signal already shows the new note
    emit meetingIdChanged();
}

void PreTranscriptViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    if (m_facade && m_facade->parent() == this && !m_facade->live()) {
        attachFacade(nullptr);   // a fictional facade serves one design state: a new one is asked for
        resolveFacade();
    }
    reload();
}

void PreTranscriptViewModel::refresh() { reload(); }

void PreTranscriptViewModel::reload()
{
    const int oldPct = m_mixdownPercent;
    applySnapshot(m_facade ? m_facade->snapshot(m_meetingId) : processing::TranscribeStepSnapshot());

    if (m_state == QLatin1String("running") && m_job.isValid())
        m_etaTimer.start();
    else
        m_etaTimer.stop();

    if (m_mixdownPercent != oldPct)
        emit mixdownPercentChanged();
    emit jobChanged();
    emit changed();
    reloadTagSuggestions();
}

void PreTranscriptViewModel::applySnapshot(const processing::TranscribeStepSnapshot& s)
{
    m_title = s.title;
    m_state = s.state;
    m_providerLabel = s.providerLabel;
    m_sttProviderId = s.sttProviderId;
    m_sttTerms = s.sttTerms;
    m_identifyAvailable = s.identifyAvailable;
    m_identifyEnabled = s.identifyEnabled;
    m_mixdownState = s.mixdownState;
    m_mixdownPercent = s.mixdownPercent;
    m_mixdownCancellable = s.mixdownCancellable;
    m_canStart = s.canStart;
    m_blocker = s.blocker;
    m_cloudSoon = s.cloudSoon;
    m_errorMessage = s.errorMessage;
    m_errorDetail = s.errorDetail;
    m_fixActionLabel = s.fixActionLabel;
    m_fixActionPage = s.fixActionPage;
    m_fixedEta = s.fixedEtaSeconds;
    m_job = JobProgress{};
    m_jobTitle.clear();
    m_stages.clear();
    m_cancellable = true;
    m_cancelling = false;
    if (m_state == QLatin1String("running"))
        applyJob(s.job);

    // The sub-models: the tag field's and the note's fictional content, or the meeting's own.
    if (!s.tagsDemoState.isEmpty())
        m_tags->setDemoState(s.tagsDemoState);
    if (s.noteDemo)
        m_note->setDemoContent(s.noteText, s.noteDetectedCallApp,
                               s.noteSuggestions ? MeetingNoteModel::demoSuggestions() : QVariantList());
    else
        m_note->reload();
    recomputeContextBudget();
}

int PreTranscriptViewModel::contextTokenBudget() const
{
    return providers::kWhisperPromptTokenBudget;
}

void PreTranscriptViewModel::recomputeContextBudget()
{
    QString state;
    int estimate = 0;
    QString preview;
    if (m_sttProviderId == QLatin1String("whisper-compat")) {
        const providers::WhisperPromptFit fit = providers::fitToBudget(m_note->note(), m_sttTerms);
        estimate = fit.fullTokens;
        const int budget = providers::kWhisperPromptTokenBudget;
        state = estimate > budget ? QStringLiteral("over")
              : estimate * 10 > budget * 7 ? QStringLiteral("hint") : QString();
        if (!fit.prompt.isEmpty() || !fit.droppedNote.isEmpty()) {
            if (!fit.droppedNote.isEmpty())
                preview = QStringLiteral("<s>%1</s> ").arg(fit.droppedNote.toHtmlEscaped());
            preview += fit.prompt.toHtmlEscaped();
        }
    }
    if (state == m_contextBudgetState && estimate == m_contextTokenEstimate && preview == m_contextPreview)
        return;
    m_contextBudgetState = state;
    m_contextTokenEstimate = estimate;
    m_contextPreview = preview;
    emit contextBudgetChanged();
}

void PreTranscriptViewModel::reloadTagSuggestions()
{
    TagBackend* b = m_tags->backend();
    QVector<TagSuggestionItem> list = m_tags->visibleSuggestions();
    m_tagSuggestionsFromModel = !list.isEmpty();
    if (list.isEmpty() && b && (m_state == QLatin1String("steps") || !live())) {
        const QString meeting = m_tags->effectiveMeetingId();
        const QStringList applied = m_tags->tagIds();
        QStringList appliedKeys;
        for (const QString& id : applied) appliedKeys << tagmatch::key(m_tags->tagName(id));
        for (const TagSuggestionItem& s : b->draftSuggestions(m_title)) {
            if ((!s.tagId.isEmpty() && applied.contains(s.tagId)) || appliedKeys.contains(tagmatch::key(s.name)))
                continue;
            if (b->isRejected(meeting, s.tagId.isEmpty() ? s.name : s.tagId))
                continue;
            list << s;
        }
    }
    if (list.size() > 3) list.resize(3);
    m_tagSuggestions = list;
    emit tagSuggestionsChanged();
}

QVariantList PreTranscriptViewModel::tagSuggestions() const
{
    QVariantList out;
    for (int i = 0; i < m_tagSuggestions.size(); ++i) {
        const TagSuggestionItem& s = m_tagSuggestions[i];
        out << QVariantMap{{QStringLiteral("index"), i}, {QStringLiteral("id"), s.tagId},
                           {QStringLiteral("name"), s.name}, {QStringLiteral("isNew"), s.isNew},
                           {QStringLiteral("source"), s.source}};
    }
    return out;
}

QString PreTranscriptViewModel::tagSuggestionReason() const
{
    if (m_tagSuggestions.isEmpty()) return {};
    const TagSuggestionItem& s = m_tagSuggestions.first();
    auto find = [&s](const char* kind) -> const TagReasonItem* {
        for (const TagReasonItem& r : s.reasons)
            if (r.kind == QLatin1String(kind) && !r.values.isEmpty()) return &r;
        return nullptr;
    };
    if (const TagReasonItem* r = find("title")) {
        const QString title = s.similarMeetings.isEmpty() ? r->values.first() : s.similarMeetings.first().title;
        return tr("hasonló cím: „%1”").arg(title);
    }
    if (const TagReasonItem* r = find("participant"))
        return tr("közös résztvevő: %1").arg(r->values.mid(0, 2).join(QStringLiteral(", ")));
    if (const TagReasonItem* r = find("terms"))
        return tr("közös kifejezések: %1").arg(r->values.mid(0, 3).join(QStringLiteral(", ")));
    return {};
}

void PreTranscriptViewModel::acceptTagSuggestion(int index)
{
    TagBackend* b = m_tags->backend();
    if (!b || index < 0 || index >= m_tagSuggestions.size()) return;
    if (m_tagSuggestionsFromModel) {   // the regular suggestions: the model asks (with a toast)
        m_tags->accept(index);
        return;
    }
    const TagSuggestionItem s = m_tagSuggestions[index];
    b->beginGroup(tr("Javaslat elfogadva: #%1").arg(s.name));
    b->addTag(m_tags->effectiveMeetingId(), s.isNew || s.tagId.isEmpty() ? s.name : s.tagId,
              TagAddSource::Suggestion);
    b->endGroup();
    reloadTagSuggestions();
    emit toast(b->undoLabel(), b->canUndo());
}

void PreTranscriptViewModel::rejectTagSuggestion(int index)
{
    TagBackend* b = m_tags->backend();
    if (!b || index < 0 || index >= m_tagSuggestions.size()) return;
    if (m_tagSuggestionsFromModel) {
        m_tags->reject(index);
        return;
    }
    const TagSuggestionItem s = m_tagSuggestions[index];
    b->beginGroup(tr("Javaslat elutasítva: #%1").arg(s.name));
    b->reject(m_tags->effectiveMeetingId(), s);
    b->endGroup();
    reloadTagSuggestions();
    emit toast(b->undoLabel(), b->canUndo());
}

void PreTranscriptViewModel::applyJob(const JobProgress& job)
{
    m_job = job;   // a design state's phases come without a task (no ETA from it)
    m_jobTitle = job.title.isEmpty() ? tr("Átírás folyamatban") : job.title;
    m_cancellable = job.cancellable;
    m_cancelling = job.cancelling;
    QVariantList list;
    for (const JobStage& s : job.stages)
        list << stageMap(s.id, s.label, jobsupport::stageStateName(s.state), s.percent, s.detail);
    m_stages = list;
}

void PreTranscriptViewModel::setContextNote(const QString& note)
{
    m_note->setNote(note);   // → noteChanged → contextNoteChanged + changed
}

void PreTranscriptViewModel::setIdentifyEnabled(bool enabled)
{
    if (enabled == m_identifyEnabled || !m_identifyAvailable)
        return;
    m_identifyEnabled = enabled;
    if (m_facade)
        m_facade->setIdentifyAfterTranscription(m_meetingId, enabled);
    emit changed();
}

QString PreTranscriptViewModel::etaText() const
{
    return jobsupport::formatEta(m_fixedEta >= 0 ? m_fixedEta : m_job.etaSeconds());
}

QString PreTranscriptViewModel::footerLine() const
{
    QStringList parts;
    QString note = m_note->note().simplified();
    if (!note.isEmpty()) {
        if (note.size() > 42)
            note = note.left(40).trimmed() + QStringLiteral("…");
        parts << tr("Kontextus: „%1”").arg(note);
    }
    if (!m_providerLabel.isEmpty())
        parts << tr("Szolgáltató: %1").arg(m_providerLabel);
    return parts.join(QStringLiteral(" · "));
}

void PreTranscriptViewModel::startMixdown()
{
    if (m_facade)
        m_facade->regenerateMixdown(m_meetingId);
}

void PreTranscriptViewModel::clearError()
{
    if (m_facade)
        m_facade->clearError(m_meetingId);   // -> changed -> reload()
}

} // namespace tanara_qml
