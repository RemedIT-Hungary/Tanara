#include "demo/VoiceModelsDemoFacade.h"

#include "demo/DemoText.h"

#include "tanara/processing/voiceid/VoiceModelRegistry.h"

using namespace tanara;
using tanara::processing::VoiceModelScenario;
using tanara::processing::VoiceprintBackfillSkip;

namespace tanara_demo {

namespace {
const QString kWespeaker = processing::VoiceModelRegistry::recommendedModelId();
const QString kCamplus = processing::VoiceModelRegistry::defaultModelId();
const QString kEres = QStringLiteral("eres2netv2");

VoiceModelScenario::Row row(const QString& state)
{
    VoiceModelScenario::Row r;
    r.state = state;
    return r;
}

VoiceprintBackfillSkip skip(const QString& person, const QString& meetingId, qint64 fromMs, qint64 toMs,
                            VoiceprintBackfillSkip::Reason reason)
{
    VoiceprintBackfillSkip s;
    s.person = person;
    s.model = kWespeaker;
    s.sourceMeetingId = meetingId;
    s.sampleRef = QStringLiteral("/demo/%1/track-01.ogg#%2-%3").arg(meetingId.isEmpty() ? QStringLiteral("x") : meetingId)
                      .arg(fromMs).arg(toMs);
    s.reason = reason;
    return s;
}
} // namespace

VoiceModelsDemoFacade::VoiceModelsDemoFacade(const QString& designState, QObject* parent)
    : IVoiceModelsFacade(parent), m_state(designState)
{
    VoiceModelScenario& sc = m_scenario;
    m_enabled = {kWespeaker};
    sc.rows.insert(kEres, row(QStringLiteral("missing")));
    const QString& state = designState;
    if (state == QLatin1String("B08download")) {
        VoiceModelScenario::Row w = row(QStringLiteral("downloading"));
        w.receivedFraction = 0.46;
        sc.rows.insert(kWespeaker, w);
    } else if (state == QLatin1String("B08none")) {
        for (const processing::VoiceModelSpec& s : processing::VoiceModelRegistry::builtin())
            sc.rows.insert(s.id, row(QStringLiteral("missing")));
    } else if (state == QLatin1String("B08error")) {
        VoiceModelScenario::Row w = row(QStringLiteral("error"));
        w.errorText = tr("A letöltés megszakadt: a kapcsolat 12 MB után megszűnt. Semmi nem került a gépre.");
        sc.rows.insert(kWespeaker, w);
    } else if (state == QLatin1String("B08offline")) {
        sc.offline = true;
        sc.rows.insert(kWespeaker, row(QStringLiteral("offline")));
        sc.rows.insert(kEres, row(QStringLiteral("offline")));
    } else if (state == QLatin1String("B08bundled")) {
        sc.rows.insert(kCamplus, row(QStringLiteral("bundled")));
    } else if (state == QLatin1String("B08fallback")) {
        // WeSpeaker is not downloaded, the bundled CAM++ runs as the fallback.
        sc.rows.insert(kWespeaker, row(QStringLiteral("missing")));
        sc.rows.insert(kCamplus, row(QStringLiteral("bundled")));
    } else if (state == QLatin1String("B08verify")) {
        // WeSpeaker was just downloaded: verification; ERes2NetV2's file is corrupt (size mismatch).
        sc.rows.insert(kWespeaker, row(QStringLiteral("verifying")));
        VoiceModelScenario::Row e = row(QStringLiteral("error"));
        e.corrupt = true;
        if (const auto spec = processing::VoiceModelRegistry::spec(kEres)) e.fileSize = spec->sizeBytes / 3;
        sc.rows.insert(kEres, e);
    }
    if (state == QLatin1String("B08backfill")) {
        sc.backfillState = QStringLiteral("running");
        sc.backfillTotal = 23;
        sc.backfillDone = 14;
    } else if (state == QLatin1String("B08partial")) {
        sc.backfillState = QStringLiteral("partial");
        sc.backfillTotal = 23;
        sc.backfillDone = 20;
        sc.backfillSkipped = 3;
    }
}

void VoiceModelsDemoFacade::setEnabledModels(const QStringList& ids)
{
    m_enabled = ids;
    emit settingsChanged();
}

QVector<VoiceprintBackfillSkip> VoiceModelsDemoFacade::backfillSkips(const QString& person) const
{
    using Reason = VoiceprintBackfillSkip::Reason;
    if (m_state == QLatin1String("B08partial")) {
        // Fictional skipped samples (person, sample, reason).
        return {skip(demoName("Nagy Péter"), QStringLiteral("demo-partner"), 760000, 772000, Reason::MissingAudio),
                skip(demoName("Szabó Bence"), QStringLiteral("demo-weekly"), 185000, 194000, Reason::MissingAudio),
                skip(demoName("Szabó Bence"), QString(), 42000, 50000, Reason::MissingMeeting)};
    }
    // The People window's note: the asked person has one sample that could not be backfilled.
    if (m_state == QLatin1String("samplesModels") && !person.isEmpty())
        return {skip(person, QStringLiteral("demo-partner"), 760000, 772000, Reason::MissingAudio)};
    return {};
}

QString VoiceModelsDemoFacade::meetingTitle(const QString& meetingId) const
{
    if (meetingId == QLatin1String("demo-partner")) return demoText("Partnerdemó – adatimport", "Partner demo – data import");
    if (meetingId == QLatin1String("demo-weekly")) return demoText("Heti egyeztetés", "Weekly sync");
    return QString();
}

} // namespace tanara_demo
