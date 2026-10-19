#include "OverviewViewModel.h"

#include "AppContext.h"

#include <QQmlEngine>

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
    // A member, owned by this view model (QML must never collect it).
    m_stepModel.setParent(this);
    QQmlEngine::setObjectOwnership(&m_stepModel, QQmlEngine::CppOwnership);
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
    m_estimates.clear();
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

bool OverviewViewModel::diarizeSpeakers(int count)
{
    if (!m_facade)
        return false;
    const bool ok = m_facade->diarizeSpeakers(m_meetingId, qMax(0, count));
    reload();
    return ok;
}

bool OverviewViewModel::decideInDemo(const QString& decision)
{
    if (!m_facade || !m_facade->decideInPlace(m_meetingId, decision))
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
    m_peopleHint = s.peopleHint;
    m_approved = s.approved;
    m_approvalPending = s.approvalPending;
    m_decision = s.decision;
    m_approveCount = s.approveCount;
    m_hiddenSpeakers = s.hiddenSpeakers;
    m_selfName = s.selfName;
    m_analysisRunning = s.analysisRunning;
    m_candidateCount = s.candidateCount;
    m_sideWho = s.sideWho;
    m_steps = s.steps;
    m_speakers = s.speakers;
    m_stats = s.stats;
    m_stepsSummary = s.stepsSummary;
    decorateSteps();
    publishSteps();
    emit changed();
}

void OverviewViewModel::publishSteps()
{
    m_stepModel.setSteps(m_steps);
}

void OverviewViewModel::requestStart(const QString& step, const QString& route)
{
    QTimer::singleShot(0, this, [this, step, route] { emit startRequested(step, route); });
}

void OverviewViewModel::requestMenu(const QString& step)
{
    QTimer::singleShot(0, this, [this, step] { emit menuRequested(step); });
}

// ---- S5: the split button ----------------------------------------------------------------

namespace {

using tanara::providers::Capability;
using tanara::providers::Route;
using tanara::providers::WorkflowStep;

std::optional<Capability> capOfStep(const QString& step)
{
    if (step == QLatin1String("transcript") || step == QLatin1String("stt")) return Capability::Stt;
    if (step == QLatin1String("summary") || step == QLatin1String("llm")) return Capability::Llm;
    return std::nullopt;
}

QString minutesText(int seconds)
{
    const int min = qMax(1, int((seconds + 30) / 60));
    return OverviewViewModel::tr("kb. %n perc", nullptr, min);
}

} // namespace

QVariantList OverviewViewModel::buildRouteOptions(const QString& step) const
{
    const auto it = m_choices.constFind(step);
    if (it == m_choices.constEnd() || !it->available) return {};
    const tanara::library::RouteChoices& c = *it;
    const Estimate est = m_estimates.value(step);
    QVariantList out;
    for (const tanara::library::RouteChoiceRow& r : c.rows) {
        QString label, reason;
        QStringList detail;
        bool enabled = r.blocker.isEmpty();
        const bool cloud = r.route == Route::Cloud;
        switch (r.route) {
        case Route::Local:
            label = tr("Helyben · %1").arg(r.provider);
            detail << tr("ingyenes");
            if (r.etaSeconds >= 0) detail << minutesText(r.etaSeconds);
            detail << tr("a gépeden");
            break;
        case Route::External:
            label = tr("Külső · %1 · saját kulcs").arg(r.provider);
            if (r.etaSeconds >= 0) detail << minutesText(r.etaSeconds);
            detail << tr("a saját fiókod díjszabásával");
            break;
        default:
            label = c.cloudLive ? tr("Tanara Cloud · %1").arg(r.provider) : tr("Tanara Cloud · hamarosan");
            break;
        }
        if (cloud && enabled) {
            if (est.done && est.value.ok) {
                detail << tr("kb. %1").arg(est.value.cost);
                if (est.value.etaSeconds >= 0) detail << minutesText(est.value.etaSeconds);
                detail << tr("a keretből");
                if (est.value.level == QLatin1String("littleReserve")) detail << tr("kevés tartalék");
                if (est.value.level == QLatin1String("notEnough")) {
                    enabled = false;
                    reason = tr("Nincs elég keret: kb. %1 kell, a keret %2.").arg(est.value.cost, est.value.balance);
                }
            } else if (est.done) {
                enabled = false;
                reason = est.value.offline ? tr("Nincs internet; a helyi út működik.")
                                           : (est.value.error.isEmpty() ? tr("Nem érkezett becslés.") : est.value.error);
            } else {
                // The estimate is the Cloud run's confirmation: the row waits for it.
                detail << QStringLiteral("…");
                if (c.confirmCloud) enabled = false;
            }
        }
        const QString& code = r.blocker;
        if (code == QLatin1String("cloud_unavailable"))
            reason = c.cloudLive ? tr("A Tanara Cloud ehhez most nem érhető el.") : tr("A Tanara Cloud még nem érhető el.");
        else if (code == QLatin1String("not_configured"))
            reason = r.detail.isEmpty() ? tr("Hiányzik egy beállítás.") : r.detail;
        else if (code == QLatin1String("no_local"))
            reason = tr("Ehhez nincs helyi szolgáltató.");
        else if (code == QLatin1String("no_external"))
            reason = tr("Ehhez nincs külső, saját kulcsos szolgáltató.");
        else if (!code.isEmpty())
            reason = tr("Ez az út most nem választható.");
        out << QVariantMap{{QStringLiteral("route"), tanara::providers::routeKey(r.route)},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("detail"), detail.join(QStringLiteral(" · "))},
                           {QStringLiteral("enabled"), enabled},
                           {QStringLiteral("reason"), reason},
                           {QStringLiteral("isDefault"), r.route == c.stored},
                           {QStringLiteral("isCloud"), cloud}};
    }
    return out;
}

void OverviewViewModel::decorateSteps()
{
    m_choices.clear();
    for (QVariant& v : m_steps) {
        QVariantMap row = v.toMap();
        const QString key = row.value(QStringLiteral("key")).toString();
        const std::optional<Capability> cap = capOfStep(key);
        if (!cap || key == QLatin1String("stt") || key == QLatin1String("llm")) continue;
        const tanara::library::RouteChoices c = m_facade ? m_facade->routeChoices(*cap, m_meetingId)
                                                         : tanara::library::RouteChoices{};
        m_choices.insert(key, c);
        row.insert(QStringLiteral("route"), c.available ? tanara::providers::routeKey(c.stored) : QString());
        row.insert(QStringLiteral("routeOptions"), buildRouteOptions(key));
        v = row;
    }
}

QVariantList OverviewViewModel::routeOptionsFor(const QString& step) const
{
    return buildRouteOptions(step);
}

void OverviewViewModel::prepareRoutes(const QString& step)
{
    const std::optional<Capability> cap = capOfStep(step);
    const auto it = m_choices.constFind(step);
    if (!cap || !m_facade || it == m_choices.constEnd() || !it->available || !it->cloudLive) return;
    bool cloudAllowed = false;
    for (const tanara::library::RouteChoiceRow& r : it->rows)
        if (r.route == Route::Cloud && r.blocker.isEmpty()) cloudAllowed = true;
    if (!cloudAllowed) return;
    Estimate& e = m_estimates[step];
    if (e.pending || (e.done && e.value.ok)) return;   // asked already (an error is asked again)
    e = Estimate{};
    e.pending = true;
    const QPointer<OverviewViewModel> self(this);
    const QString meetingId = m_meetingId;
    m_facade->estimateCloud(*cap == Capability::Stt ? WorkflowStep::Transcribe : WorkflowStep::Summarize, meetingId,
                            [self, step, meetingId](const tanara::library::CloudRunEstimate& value) {
        if (!self || self->m_meetingId != meetingId) return;
        Estimate& slot = self->m_estimates[step];
        slot.pending = false;
        slot.done = true;
        slot.value = value;
        self->decorateSteps();
        self->publishSteps();
        emit self->changed();
    });
}

bool OverviewViewModel::startDefault(const QString& step)
{
    const auto it = m_choices.constFind(step);
    if (it != m_choices.constEnd() && it->available) {
        const bool cloudConfirm = it->stored == Route::Cloud && it->cloudLive && it->confirmCloud;
        if (it->stored == Route::Ask || cloudConfirm) {
            prepareRoutes(step);
            requestMenu(step);
            return false;
        }
    }
    requestStart(step, QString());
    return true;
}

bool OverviewViewModel::start(const QString& step, const QString& route)
{
    for (const QVariant& v : buildRouteOptions(step)) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("route")).toString() != route) continue;
        if (!row.value(QStringLiteral("enabled")).toBool()) {
            QString reason = row.value(QStringLiteral("reason")).toString();
            if (reason.isEmpty()) reason = tr("Várd meg a költségbecslést.");
            emit routeRejected(step, reason);
            return false;
        }
        requestStart(step, route);
        return true;
    }
    emit routeRejected(step, tr("Ez az út most nem választható."));
    return false;
}

bool OverviewViewModel::setDefaultRoute(const QString& cap, const QString& route)
{
    const std::optional<Capability> c = capOfStep(cap);
    const std::optional<Route> r = tanara::providers::routeFromKey(route);
    if (!c || !r || !m_facade) return false;
    const bool ok = m_facade->setDefaultRoute(*c, *r);
    if (ok) reload();
    return ok;
}

} // namespace tanara_qml
