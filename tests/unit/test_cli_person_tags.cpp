//
// tanara-cli tags people / person / set-person: a valódi CLI (QProcess), IZOLÁLT TANARA_HOME-mal,
// kitalált megbeszélésekkel (a tesztfolyamat egy AppControllerrel írja őket a homokozóba).
//
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

using namespace tanara;

namespace {

struct Run { int code = -1; QString out; QString err; };

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGabor = QStringLiteral("Fehér Gábor");

} // namespace

class CliPersonTagsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppController app;
        app.setUserSpeakerName(kSelf);
        const Tag nord = app.tags()->create(QStringLiteral("Nordvik"));
        for (int i = 1; i <= 3; ++i) {
            Meeting m = app.store()->createMeeting(QStringLiteral("Nordvik %1").arg(i));
            m.startedAt = QDateTime(QDate(2026, 9, i), QTime(10, 0));
            Track mic;
            mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg";
            mic.speakerLabel = kSelf; mic.fixedSpeaker = true; mic.active = true;
            m.tracks.append(mic);
            m.speakerMap.insert(QStringLiteral("Beszélő 2"), kGabor);
            m.tagIds = {nord.id};
            app.store()->saveMeeting(m);
        }
        qunsetenv("TANARA_HOME");
    }
    void cleanup() { m_home.reset(); }

    void learnedAndManual()
    {
        Run r = cli({"tags", "people", "Nordvik"});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains(kGabor + "  3 / 3  [learned]"), qPrintable(r.out));
        QVERIFY(!r.out.contains(kSelf));
        QVERIFY(r.out.contains("Suggested: +" + kGabor));

        r = cli({"tags", "person", kGabor});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("#Nordvik  3 / 3"), qPrintable(r.out));
        QVERIFY(r.out.contains("Suggested: +#Nordvik"));

        r = cli({"tags", "set-person", kGabor, "Nordvik,Partnerek"});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains(kGabor + ": #Nordvik #Partnerek"), qPrintable(r.out));

        r = cli({"tags", "person", kGabor});
        QVERIFY(r.out.contains("#Nordvik  3 / 3  [tagged]"));
        QVERIFY(r.out.contains("#Partnerek  0 / 0  [tagged]"));
        QVERIFY(!r.out.contains("Suggested"));

        r = cli({"tags", "people", "Partnerek"});
        QVERIFY(r.out.contains(kGabor + "  0 / 0  [tagged]"));

        r = cli({"tags", "set-person", kGabor, ""});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("(no tags)"));
    }

    void errors()
    {
        QCOMPARE(cli({"tags", "people"}).code, 1);
        QCOMPARE(cli({"tags", "people", "Nincs ilyen"}).code, 1);
        QCOMPARE(cli({"tags", "set-person", kGabor}).code, 1);
        const Run r = cli({"tags", "set-person", kSelf, "Nordvik"});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("own person"));
    }

private:
    Run cli(const QStringList& args)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("TANARA_HOME", m_home->path());
        env.insert("TANARA_CLOUD", "off");
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(TANARA_CLI_EXE), args);
        Run r;
        if (!p.waitForFinished(120000)) { p.kill(); return r; }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        r.out = QString::fromUtf8(p.readAllStandardOutput());
        r.err = QString::fromUtf8(p.readAllStandardError());
        return r;
    }

    std::unique_ptr<QTemporaryDir> m_home;
};

QTEST_GUILESS_MAIN(CliPersonTagsTest)
#include "test_cli_person_tags.moc"
