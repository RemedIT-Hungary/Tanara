#include "OverviewViewModel.h"

#include "AppContext.h"

#include "tanara/library/IOverviewFacade.h"

namespace tanara_qml {

OverviewViewModel::OverviewViewModel(QObject* parent) : QObject(parent)
{
    init();
    // Without an injected facade the model follows the App singleton's services.
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this] { resolve(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this] { resolve(); reload(); });
    resolve();
    reload();
}

OverviewViewModel::OverviewViewModel(tanara::library::IOverviewFacade* facade, QObject* parent)
    : QObject(parent), m_injected(true)
{
    init();
    attach(facade);
    reload();
}

void OverviewViewModel::init()
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &OverviewViewModel::reload);
}

void OverviewViewModel::resolve()
{
    if (m_injected)
        return;
    // The real facade is shared; a fictional one serves this model's design state and is its child.
    attach(AppContext::instance()->services().overview(m_demoState, this));
}

void OverviewViewModel::attach(tanara::library::IOverviewFacade* facade)
{
    if (facade == m_facade)
        return;
    if (m_facade) {
        m_facade->disconnect(this);
        if (m_facade->parent() == this && !m_injected)
            delete m_facade.data();
    }
    m_facade = facade;
    if (facade)
        connect(facade, &tanara::library::IOverviewFacade::changed, this, [this](const QString& id) {
            if (id == m_meetingId) scheduleReload();
        });
}

void OverviewViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

void OverviewViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    resolve();   // a fictional facade serves one design state
    reload();
}

void OverviewViewModel::scheduleReload()
{
    m_reloadTimer.start();
}

QString OverviewViewModel::monogramOf(const QString& name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString& w : words) {
        if (w.startsWith(QLatin1Char('('))) continue;   // "Kovács Lilla (you)"
        out += w.left(1).toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

void OverviewViewModel::refresh()
{
    reload();
}

QString OverviewViewModel::addParticipant(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || !m_facade)
        return {};
    const QString id = m_facade->addParticipant(m_meetingId, n);
    reload();
    return id;
}

void OverviewViewModel::removeParticipant(const QString& id)
{
    if (!m_facade)
        return;
    m_facade->removeParticipant(m_meetingId, id);
    reload();
}

bool OverviewViewModel::nameParticipant(const QString& id, const QString& personIdOrName)
{
    if (!m_facade || id.isEmpty() || personIdOrName.trimmed().isEmpty())
        return false;
    const bool ok = m_facade->nameParticipant(m_meetingId, id, personIdOrName.trimmed());
    reload();
    return ok;
}

bool OverviewViewModel::mergeParticipantInto(const QString& fromId, const QString& intoId)
{
    if (!m_facade || fromId.isEmpty() || intoId.isEmpty())
        return false;
    const bool ok = m_facade->mergeParticipantInto(m_meetingId, fromId, intoId);
    reload();
    return ok;
}

bool OverviewViewModel::confirmParticipant(const QString& id, bool confirmed)
{
    if (!m_facade || id.isEmpty())
        return false;
    const bool ok = m_facade->confirmParticipant(m_meetingId, id, confirmed);
    reload();
    return ok;
}

QVariantMap OverviewViewModel::setSelfParticipant(const QString& id)
{
    QString why;
    const bool ok = m_facade && !id.isEmpty() && m_facade->setSelfParticipant(m_meetingId, id, &why);
    reload();
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("reason"), ok ? QString() : why}};
}

bool OverviewViewModel::setSolo()
{
    if (!m_facade)
        return false;
    const bool ok = m_facade->setSolo(m_meetingId);
    reload();
    return ok;
}

bool OverviewViewModel::reanalyze()
{
    if (!m_facade)
        return false;
    const bool ok = m_facade->reanalyze(m_meetingId);
    reload();
    return ok;
}

bool OverviewViewModel::nameSpeakerInDemo(const QString& speakerKey, const QString& name)
{
    if (!m_facade || !m_facade->nameSpeakerInPlace(m_meetingId, speakerKey, name))
        return false;
    reload();
    return true;
}

void OverviewViewModel::setContextNote(const QString& note)
{
    if (!m_facade)
        return;
    m_facade->setContextNote(m_meetingId, note);
    reload();
}

void OverviewViewModel::reload()
{
    m_reloadTimer.stop();
    const tanara::library::OverviewSnapshot s = m_facade ? m_facade->snapshot(m_meetingId)
                                                     : tanara::library::OverviewSnapshot{};
    m_hasTranscript = s.hasTranscript;
    m_title = s.title;
    m_canReanalyze = s.canReanalyze;
    m_transcribing = s.transcribing;
    m_allDone = s.allDone;
    m_contextNote = s.contextNote;
    m_peopleState = s.peopleState;
    m_participants = s.participants;
    m_anonymous = s.anonymousSpeakers;
    m_peopleHint = s.peopleHint;
    m_approved = s.approved;
    m_approvalPending = s.approvalPending;
    m_analysisRunning = s.analysisRunning;
    m_candidateCount = s.candidateCount;
    m_sideWho = s.sideWho;
    m_steps = s.steps;
    m_stats = s.stats;
    m_stepsSummary = s.stepsSummary;
    emit changed();
}

} // namespace tanara_qml
