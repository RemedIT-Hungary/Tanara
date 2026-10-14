#include "demo/ShellDemoFacade.h"

#include "demo/LibraryDemoData.h"

namespace tanara_demo {

using tanara::JobKind;

ShellDemoFacade::ShellDemoFacade(QObject* parent) : tanara::editing::IShellFacade(parent) {}

tanara::Meeting ShellDemoFacade::meeting(const QString& meetingId) const
{
    const DemoMeeting* d = find(meetingId);
    if (!d)
        return {};
    // Header data only: no folder, no files.
    tanara::Meeting m;
    m.id = d->entry.id;
    m.title = d->entry.title;
    m.startedAt = d->entry.startedAt;
    m.durationMs = d->entry.durationMs;
    m.hasTranscript = d->entry.hasTranscript;
    m.hasSummary = d->entry.hasSummary;
    // At least one active track: the sample meetings can all be identified.
    for (int i = 0; i < qMax(1, d->tracks); ++i) {
        tanara::Track t;
        t.id = QStringLiteral("demo-track-%1").arg(i + 1);
        m.tracks.append(t);
    }
    return m;
}

bool ShellDemoFacade::meetingExists(const QString& meetingId) const
{
    return !meetingId.isEmpty() && find(meetingId) != nullptr;
}

QString ShellDemoFacade::meetingTitle(const QString& meetingId) const
{
    const DemoMeeting* d = find(meetingId);
    return d ? d->entry.title : QString();
}

int ShellDemoFacade::speakerCount(const tanara::Meeting& meeting) const
{
    const DemoMeeting* d = find(meeting.id);
    return d ? d->speakers : 0;
}

tanara::SummaryStaleInfo ShellDemoFacade::summaryStale(const tanara::Meeting& meeting) const
{
    tanara::SummaryStaleInfo info;
    if (const DemoMeeting* d = find(meeting.id)) {
        info.stale = d->entry.state.summaryStale;
        info.correctedSpeakers = d->entry.state.staleCorrectedSpeakers;
    }
    return info;
}

QString ShellDemoFacade::providerLabel(tanara::providers::WorkflowStep) const
{
    return tr("LM Studio · saját kulcs");
}

QVector<tanara::JobProgress> ShellDemoFacade::sampleTasks(const QString& meetingId) const
{
    // The task bar's sample row ("Identifying participants", 3 / 5).
    tanara::JobProgress job;
    job.meetingId = meetingId.isEmpty() ? QStringLiteral("demo") : meetingId;
    job.kind = JobKind::Identify;
    job.title = tr("Résztvevők azonosítása");
    job.done = 3;
    job.total = 5;
    return {job};
}

tanara::providers::ReadinessResult ShellDemoFacade::canRun(tanara::providers::WorkflowStep, const QString&) const
{
    tanara::providers::ReadinessResult r;
    r.runnable = true;   // the shell never gets here (live() is false), but nothing is blocked
    return r;
}

tanara::editing::RetranscribeImpact ShellDemoFacade::retranscribeImpact(const QString&) const
{
    // The numbers of the sample: 23 manual corrections, nobody named.
    tanara::editing::RetranscribeImpact imp;
    imp.correctedUtterances = 23;
    return imp;
}

} // namespace tanara_demo
