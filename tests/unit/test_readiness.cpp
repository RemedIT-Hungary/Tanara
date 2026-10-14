// Tanara: ReadinessModel (gating logic) unit tests.
//
// ReadinessModel is the ONE source of truth for "can a workflow step run, and if not,
// EXACTLY what is missing?". The GUI's button enabling AND AppController's guards both use it.
// Here we check the Transcribe/Summarize gates: whether a missing meeting state (no active
// track / no transcript) or provider config (a missing required field / secret) blocks correctly.
//
// IMPORTANT: the tests/unit/*.cpp files compile into SEPARATE test exes (see
// tests/CMakeLists.txt GLOB), each bringing its OWN main. Core (tanara_core) does NOT link Qt
// Widgets, hence QTEST_GUILESS_MAIN here (no QApplication).
#include <QtTest>
#include "tanara/app/AppSettings.h"
#include "tanara/providers/core/ProviderConfig.h"
#include <QObject>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QStringList>

#include "tanara/domain/Types.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/ReadinessModel.h"

using namespace tanara;

namespace {

// An tanara::app::AppSettings with the built-in default providers (soniox / openai-compat), filled so that
// the required NON-secret fields (e.g. the LLM baseUrl) are present. Each test breaks only what it examines.
tanara::app::AppSettings makeSettings()
{
    tanara::app::AppSettings s;
    s.providers.stt.selectedId = QStringLiteral("soniox");
    s.providers.llm.selectedId = QStringLiteral("openai-compat");

    providers::ProviderConfig stt;
    stt.type    = QStringLiteral("soniox");
    stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
    stt.model   = QStringLiteral("stt-async-v5");
    s.providers.stt.configs.insert(QStringLiteral("soniox"), stt);

    providers::ProviderConfig llm;
    llm.type    = QStringLiteral("openai-compat");
    llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
    llm.model   = QStringLiteral("google/gemma-4-12b");
    s.providers.llm.configs.insert(QStringLiteral("openai-compat"), llm);

    return s;
}

// A meeting with ONE active audio track (the recording happened).
Meeting meetingWithActiveTrack()
{
    Meeting m;
    m.id    = QStringLiteral("m1");
    m.title = QStringLiteral("Teszt-meeting");

    Track t;
    t.id     = QStringLiteral("mic");
    t.active = true;
    m.tracks.append(t);

    return m;
}

// A SecretProbe that returns false for EVERY secret (no key is set at all).
providers::ReadinessModel::SecretProbe noSecrets()
{
    return [](const QString&) { return false; };
}

// A SecretProbe that returns true ONLY for the listed keys.
providers::ReadinessModel::SecretProbe secretsPresent(QStringList keys)
{
    return [keys](const QString& k) { return keys.contains(k); };
}

// Writes a valid (non-empty) transcript.tokens.json into the meeting's folder, in
// AppController's writeTokensJson format (root.tokens is a non-empty array).
// The caller provides the folder parameter from a QTemporaryDir.
void writeTranscriptFile(const QString& folder)
{
    QFile f(QDir(folder).filePath(QStringLiteral("transcript.tokens.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral(
        "{\"language\":\"hu\",\"tokens\":["
        "{\"text\":\"Sziasztok\",\"speaker\":\"Te\",\"startMs\":0,\"endMs\":900,"
        "\"confidence\":0.99,\"trackId\":\"mic\"}]}"));
    f.close();
}

} // namespace

class ReadinessTests : public QObject {
    Q_OBJECT
private slots:

    // The descriptors' (Soniox / OpenAI-compat) required fields need the built-in providers: we
    // ensure registration before every test (idempotent, see registerBuiltinProviders()).
    void initTestCase()
    {
        providers::registerBuiltinProviders();
    }

    // Transcribe blocks if there is NO (active) audio track: the recording has not happened yet.
    // The meeting state is the blocker, the CTA leads to recording ("record").
    void transcribe_blocksWhenNoActiveTrack()
    {
        // (a) completely empty tracks
        {
            providers::ReadinessModel model(makeSettings().providers, secretsPresent({ QStringLiteral("soniox.apiKey") }));
            Meeting m;
            m.id = QStringLiteral("m-empty");

            const providers::ReadinessResult r = model.check(providers::WorkflowStep::Transcribe, m);
            QVERIFY(!r.runnable);
            QCOMPARE(r.blockerKind, providers::BlockerKind::MeetingState);
            QCOMPARE(r.fixActionHint, QStringLiteral("record"));
        }

        // (b) there is a track, but all inactive (judged silent after the recording)
        {
            providers::ReadinessModel model(makeSettings().providers, secretsPresent({ QStringLiteral("soniox.apiKey") }));
            Meeting m = meetingWithActiveTrack();
            m.tracks[0].active = false;

            const providers::ReadinessResult r = model.check(providers::WorkflowStep::Transcribe, m);
            QVERIFY(!r.runnable);
            QCOMPARE(r.blockerKind, providers::BlockerKind::MeetingState);
            QCOMPARE(r.fixActionHint, QStringLiteral("record"));
        }
    }

    // Transcribe blocks if there is an active track, BUT the chosen STT's (Soniox) required
    // secret (apiKey) is missing. The provider config is the blocker, the missing field is "apiKey".
    void transcribe_blocksWhenSonioxKeyMissing()
    {
        providers::ReadinessModel model(makeSettings().providers, noSecrets());   // hasSecret is always false
        const Meeting m = meetingWithActiveTrack();          // there is an active track

        const providers::ReadinessResult r = model.check(providers::WorkflowStep::Transcribe, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, providers::BlockerKind::ProviderConfig);
        QCOMPARE(r.providerId, QStringLiteral("soniox"));
        QCOMPARE(r.missingFieldKey, QStringLiteral("apiKey"));
    }

    // Transcribe is RUNNABLE if there is an active track AND the Soniox key is set.
    void transcribe_runnableWithTrackAndKey()
    {
        providers::ReadinessModel model(makeSettings().providers,
                             secretsPresent({ QStringLiteral("soniox.apiKey") }));
        const Meeting m = meetingWithActiveTrack();

        const providers::ReadinessResult r = model.check(providers::WorkflowStep::Transcribe, m);
        QVERIFY(r.runnable);
    }

    // Summarize blocks if there is NO transcript (Transcribe has not run yet). The meeting state
    // is the blocker, the detail refers to the transcript. The meeting's folder is empty (no
    // transcript.tokens.json) AND hasTranscript == false.
    void summarize_blocksWhenNoTranscript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        providers::ReadinessModel model(makeSettings().providers,
                             secretsPresent({ QStringLiteral("llm.apiKey") }));

        Meeting m = meetingWithActiveTrack();
        m.folder        = dir.path();   // empty folder: no transcript file
        m.hasTranscript = false;

        const providers::ReadinessResult r = model.check(providers::WorkflowStep::Summarize, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, providers::BlockerKind::MeetingState);
        QVERIFY(!r.detail.isEmpty());
    }

    // Summarize blocks if there IS a transcript, BUT the chosen LLM's (openai-compat) required
    // field (baseUrl) is missing. This is the case that used to be NOT validated.
    // The provider config is the blocker, providerId "openai-compat", the missing field is "baseUrl".
    void summarize_blocksWhenLlmRequiredFieldMissing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeTranscriptFile(dir.path());

        tanara::app::AppSettings s = makeSettings();
        s.providers.llm.configs[QStringLiteral("openai-compat")].baseUrl = QString();   // the required field is empty

        providers::ReadinessModel model(s.providers, secretsPresent({ QStringLiteral("llm.apiKey") }));

        Meeting m = meetingWithActiveTrack();
        m.folder        = dir.path();   // there is a transcript.tokens.json
        m.hasTranscript = true;

        const providers::ReadinessResult r = model.check(providers::WorkflowStep::Summarize, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, providers::BlockerKind::ProviderConfig);
        QCOMPARE(r.providerId, QStringLiteral("openai-compat"));
        QCOMPARE(r.missingFieldKey, QStringLiteral("baseUrl"));
    }

    // Tanara Cloud (AuthMode::Login): a provider without a key field, when not signed in, gives
    // a "Sign in to Tanara Cloud" auth block (CTA: login:tanara-cloud); the sign-in is decided by
    // the descriptor's loginSecretKey (tanara.cloud.apiKey).
    void cloud_blocksUntilLoggedIn()
    {
        providers::registerCloudProviders();
        tanara::app::AppSettings s = makeSettings();
        s.providers.stt.selectedId = QStringLiteral("tanara-cloud");
        s.providers.llm.selectedId = QStringLiteral("tanara-cloud");
        const Meeting m = meetingWithActiveTrack();

        providers::ReadinessModel out(s.providers, noSecrets());
        const providers::ReadinessResult r = out.check(providers::WorkflowStep::Transcribe, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, providers::BlockerKind::Auth);
        QCOMPARE(r.fixActionHint, QStringLiteral("login:tanara-cloud"));
        QCOMPARE(r.detail, QStringLiteral("Jelentkezz be a Tanara Cloudba"));

        providers::ReadinessModel in(s.providers, secretsPresent({ QStringLiteral("tanara.cloud.apiKey") }));
        QVERIFY(in.check(providers::WorkflowStep::Transcribe, m).runnable);
        // The old "<id>.token" convention does not count as a sign-in for the cloud.
        providers::ReadinessModel legacy(s.providers, secretsPresent({ QStringLiteral("tanara-cloud.token") }));
        QVERIFY(!legacy.check(providers::WorkflowStep::Transcribe, m).runnable);
    }
};

QTEST_GUILESS_MAIN(ReadinessTests)
#include "test_readiness.moc"
