//
// Tanara — ReadinessModel (kapuzás-logika) unit-tesztek.
//
// A ReadinessModel az EGY igazságforrás arra, hogy „futtatható-e egy
// munkafolyamat-lépés, és ha nem, PONTOSAN mi hiányzik?". A GUI gomb-engedélyezés
// ÉS az AppController guardjai is ezt használják. Itt a Transcribe/Summarize
// kapukat ellenőrizzük: meeting-állapot (nincs aktív sáv / nincs átirat) vagy
// provider-konfig (hiányzó kötelező mező / titok) hiánya blokkol-e helyesen.
//
// FONTOS: a tests/unit/*.cpp fájlok KÜLÖN teszt-exe-be fordulnak (lásd
// tests/CMakeLists.txt GLOB), mindegyik hozza a SAJÁT main-jét. A core (tanara_core)
// NEM linkel Qt Widgetset, ezért itt QTEST_GUILESS_MAIN (nincs QApplication).
//
#include <QtTest>
#include <QObject>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QStringList>

#include "tanara/Types.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/provider/ReadinessModel.h"

using namespace tanara;

namespace {

// Egy AppSettings a beépített default providerekkel (soniox / openai-compat),
// úgy feltöltve, hogy a kötelező NEM-titkos mezők (pl. az LLM baseUrl) megvannak.
// Az egyes tesztek ebből csak azt rontják el, amit vizsgálnak.
AppSettings makeSettings()
{
    AppSettings s;
    s.sttProviderId = QStringLiteral("soniox");
    s.llmProviderId = QStringLiteral("openai-compat");

    ProviderConfig stt;
    stt.type    = QStringLiteral("soniox");
    stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
    stt.model   = QStringLiteral("stt-async-v5");
    s.sttConfigs.insert(QStringLiteral("soniox"), stt);

    ProviderConfig llm;
    llm.type    = QStringLiteral("openai-compat");
    llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
    llm.model   = QStringLiteral("google/gemma-4-12b");
    s.llmConfigs.insert(QStringLiteral("openai-compat"), llm);

    return s;
}

// Egy meeting EGY aktív hangsávval (a felvétel megtörtént).
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

// SecretProbe, ami MINDEN titokra false-t ad (nincs egyetlen kulcs sem beállítva).
ReadinessModel::SecretProbe noSecrets()
{
    return [](const QString&) { return false; };
}

// SecretProbe, ami CSAK a felsorolt kulcsokra ad true-t.
ReadinessModel::SecretProbe secretsPresent(QStringList keys)
{
    return [keys](const QString& k) { return keys.contains(k); };
}

// Egy érvényes (nem-üres) transcript.tokens.json kiírása a meeting mappájába,
// az AppController writeTokensJson formátumában (root.tokens nem-üres tömb).
// A folder paramétert a hívó egy QTemporaryDir-ből adja.
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

    // A descriptorok (Soniox / OpenAI-compat) kötelező mezőihez kellenek a
    // beépített providerek — minden teszt előtt biztosítjuk a regisztrációt
    // (idempotens, lásd registerBuiltinProviders()).
    void initTestCase()
    {
        registerBuiltinProviders();
    }

    // Transcribe blokkol, ha NINCS (aktív) hangsáv: a felvétel még nem történt meg.
    // → meeting-állapot a blokkoló, a CTA a felvételre visz ("record").
    void transcribe_blocksWhenNoActiveTrack()
    {
        // (a) teljesen üres tracks
        {
            ReadinessModel model(makeSettings(), secretsPresent({ QStringLiteral("soniox.apiKey") }));
            Meeting m;
            m.id = QStringLiteral("m-empty");

            const ReadinessResult r = model.check(WorkflowStep::Transcribe, m);
            QVERIFY(!r.runnable);
            QCOMPARE(r.blockerKind, BlockerKind::MeetingState);
            QCOMPARE(r.fixActionHint, QStringLiteral("record"));
        }

        // (b) van sáv, de mind inaktív (felvétel után csendesnek ítélve)
        {
            ReadinessModel model(makeSettings(), secretsPresent({ QStringLiteral("soniox.apiKey") }));
            Meeting m = meetingWithActiveTrack();
            m.tracks[0].active = false;

            const ReadinessResult r = model.check(WorkflowStep::Transcribe, m);
            QVERIFY(!r.runnable);
            QCOMPARE(r.blockerKind, BlockerKind::MeetingState);
            QCOMPARE(r.fixActionHint, QStringLiteral("record"));
        }
    }

    // Transcribe blokkol, ha van aktív sáv, DE a kiválasztott STT (Soniox) kötelező
    // titka (apiKey) hiányzik. → provider-konfig a blokkoló, a hiányzó mező az "apiKey".
    void transcribe_blocksWhenSonioxKeyMissing()
    {
        ReadinessModel model(makeSettings(), noSecrets());   // hasSecret mindig false
        const Meeting m = meetingWithActiveTrack();          // van aktív sáv

        const ReadinessResult r = model.check(WorkflowStep::Transcribe, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, BlockerKind::ProviderConfig);
        QCOMPARE(r.providerId, QStringLiteral("soniox"));
        QCOMPARE(r.missingFieldKey, QStringLiteral("apiKey"));
    }

    // Transcribe FUTTATHATÓ, ha van aktív sáv ÉS a Soniox-kulcs be van állítva.
    void transcribe_runnableWithTrackAndKey()
    {
        ReadinessModel model(makeSettings(),
                             secretsPresent({ QStringLiteral("soniox.apiKey") }));
        const Meeting m = meetingWithActiveTrack();

        const ReadinessResult r = model.check(WorkflowStep::Transcribe, m);
        QVERIFY(r.runnable);
    }

    // Summarize blokkol, ha NINCS átirat (a Transcribe még nem futott). A blokkoló
    // meeting-állapot, a detail az átiratra utal. A meeting mappája üres (nincs
    // transcript.tokens.json) ÉS hasTranscript == false.
    void summarize_blocksWhenNoTranscript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ReadinessModel model(makeSettings(),
                             secretsPresent({ QStringLiteral("llm.apiKey") }));

        Meeting m = meetingWithActiveTrack();
        m.folder        = dir.path();   // üres mappa → nincs átirat-fájl
        m.hasTranscript = false;

        const ReadinessResult r = model.check(WorkflowStep::Summarize, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, BlockerKind::MeetingState);
        QVERIFY(!r.detail.isEmpty());
    }

    // Summarize blokkol, ha VAN átirat, DE a kiválasztott LLM (openai-compat)
    // kötelező mezője (baseUrl, required) hiányzik. Ez a régen NEM-validált eset.
    // → provider-konfig a blokkoló, providerId "openai-compat", a hiányzó mező a "baseUrl".
    void summarize_blocksWhenLlmRequiredFieldMissing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeTranscriptFile(dir.path());

        AppSettings s = makeSettings();
        s.llmConfigs[QStringLiteral("openai-compat")].baseUrl = QString();   // required mező üres

        ReadinessModel model(s, secretsPresent({ QStringLiteral("llm.apiKey") }));

        Meeting m = meetingWithActiveTrack();
        m.folder        = dir.path();   // van transcript.tokens.json
        m.hasTranscript = true;

        const ReadinessResult r = model.check(WorkflowStep::Summarize, m);
        QVERIFY(!r.runnable);
        QCOMPARE(r.blockerKind, BlockerKind::ProviderConfig);
        QCOMPARE(r.providerId, QStringLiteral("openai-compat"));
        QCOMPARE(r.missingFieldKey, QStringLiteral("baseUrl"));
    }
};

QTEST_GUILESS_MAIN(ReadinessTests)
#include "test_readiness.moc"
