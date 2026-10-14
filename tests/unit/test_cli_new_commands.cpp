// Smoke tests of the CLI commands added with the command registry (P14): mixdown, autostart,
// running-apps, providers, people. The real CLI (QProcess), isolated TANARA_HOME; no device, no network.
#include "cli_people_fixture.h"
#include "cli_run.h"

#include <QTemporaryDir>
#include <QtTest>

using cli_fixture::runCli;

class CliNewCommandsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        QVERIFY(cli_fixture::writePeople(m_home->path(), {{"p-anna", "Anna"}, {"p-bela", "Béla"}}));
    }
    void cleanup() { m_home.reset(); }

    void mixdownArguments()
    {
        QCOMPARE(runCli(m_home->path(), {"mixdown"}).code, 2);
        const auto r = runCli(m_home->path(), {"mixdown", "no-such-meeting"});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("No such meeting"));
    }

    void autostartIsGuardedInTheSandbox()
    {
        const auto st = runCli(m_home->path(), {"autostart", "status"});
        QCOMPARE(st.code, 0);
        QVERIFY(st.out.contains("Manageable here: no"));
        for (const char* sub : {"on", "off"}) {
            const auto r = runCli(m_home->path(), {"autostart", sub});
            QCOMPARE(r.code, 1);
            QVERIFY(r.err.contains("nothing changed"));
        }
        QCOMPARE(runCli(m_home->path(), {"autostart"}).code, 2);
        QCOMPARE(runCli(m_home->path(), {"autostart", "maybe"}).code, 2);
    }

    void runningAppsListsTheWatchedApps()
    {
        const auto r = runCli(m_home->path(), {"running-apps"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("Watched call apps ("));
    }

    void providersList()
    {
        const auto r = runCli(m_home->path(), {"providers", "list"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("soniox"));
        QVERIFY(r.out.contains("openai-compat"));
        QVERIFY(r.out.contains("llm:"));
    }

    void providersTestRefusesWithoutTouchingTheNetwork()
    {
        const auto unknown = runCli(m_home->path(), {"providers", "test", "no-such-provider"});
        QCOMPARE(unknown.code, 2);
        QVERIFY(unknown.err.contains("No such provider"));
        // A login provider has no probe: refused before any request.
        const auto login = runCli(m_home->path(), {"providers", "test", "tanara-hosted-embedding"});
        QCOMPARE(login.code, 2);
        QVERIFY(login.err.contains("no connection test"));
        QCOMPARE(runCli(m_home->path(), {"providers", "test"}).code, 2);
        QCOMPARE(runCli(m_home->path(), {"providers"}).code, 2);
    }

    void peopleListAndMerge()
    {
        const auto list = runCli(m_home->path(), {"people", "list"});
        QCOMPARE(list.code, 0);
        QVERIFY(list.out.contains("Anna") && list.out.contains("Béla"));

        QCOMPARE(runCli(m_home->path(), {"people", "merge", "Anna", "Anna"}).code, 1);
        QCOMPARE(runCli(m_home->path(), {"people", "merge", "Anna", "Nobody"}).code, 1);
        QCOMPARE(runCli(m_home->path(), {"people", "merge", "Anna"}).code, 2);

        const auto merged = runCli(m_home->path(), {"people", "merge", "Anna", "Béla"});
        QCOMPARE(merged.code, 0);
        QVERIFY(merged.out.contains("Merged \"Béla\" into \"Anna\""));

        const auto after = runCli(m_home->path(), {"people", "list"});
        QVERIFY(after.out.contains("Anna"));
        QVERIFY(after.out.contains("aka Béla"));
        QVERIFY(!after.out.contains("  Béla  ["));
    }

private:
    std::unique_ptr<QTemporaryDir> m_home;
};

QTEST_GUILESS_MAIN(CliNewCommandsTest)
#include "test_cli_new_commands.moc"
