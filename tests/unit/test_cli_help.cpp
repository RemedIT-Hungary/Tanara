// tanara-cli help: every registered command is listed under its module, `help <command>` prints the
// usage, an unknown command fails with a suggestion. Runs the real CLI (QProcess), isolated TANARA_HOME.
#include "cli_run.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using cli_fixture::runCli;

class CliHelpTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(m_home.isValid()); }

    void everyCommandIsListedUnderItsModule()
    {
        const auto json = runCli(m_home.path(), {"help", "--json"});
        QCOMPARE(json.code, 0);
        const QJsonArray all = QJsonDocument::fromJson(json.out.toUtf8()).array();
        QVERIFY(all.size() >= 30);

        const auto text = runCli(m_home.path(), {"help"});
        QCOMPARE(text.code, 0);
        const QStringList lines = text.out.split('\n');
        QString module;
        QSet<QString> seen;
        for (const QString& l : lines) {
            if (l.isEmpty()) continue;
            if (!l.startsWith(' ')) { module = l.trimmed(); continue; }
            const QString name = l.trimmed().section(' ', 0, 0);
            seen.insert(module + "/" + name);
        }
        for (const QJsonValue& v : all) {
            const QJsonObject c = v.toObject();
            QVERIFY2(seen.contains(c["module"].toString() + "/" + c["name"].toString()),
                     qPrintable("not listed: " + c["name"].toString()));
            QVERIFY(!c["summary"].toString().isEmpty());
            QVERIFY(!c["usage"].toString().isEmpty());
        }
    }

    void theCommandSetOfTheContract()
    {
        const auto json = runCli(m_home.path(), {"help", "--json"});
        QSet<QString> names;
        for (const QJsonValue& v : QJsonDocument::fromJson(json.out.toUtf8()).array())
            names.insert(v.toObject()["name"].toString());
        const QStringList expected{
            "devices", "record", "power-probe", "mixdown", "detect", "autostart", "running-apps", "list", "import", "export",
            "import-archive", "reindex", "rename", "tags", "adopt", "align", "paths", "library", "transcribe",
            "summarize", "identify", "voice-models", "voice-eval", "embed-probe", "providers", "review", "lines",
            "reslice", "participants", "track-sides", "voiceprints", "people", "cloud", "migrate", "settings"};
        for (const QString& n : expected) QVERIFY2(names.contains(n), qPrintable("missing: " + n));
    }

    void helpOfACommandPrintsItsUsage()
    {
        const auto json = runCli(m_home.path(), {"help", "--json"});
        for (const QJsonValue& v : QJsonDocument::fromJson(json.out.toUtf8()).array()) {
            const QString name = v.toObject()["name"].toString();
            const auto r = runCli(m_home.path(), {"help", name});
            QVERIFY2(r.code == 0, qPrintable(name));
            QVERIFY2(r.out.contains("Usage:") && r.out.contains("tanara-cli " + v.toObject()["usage"].toString().section('\n', 0, 0)),
                     qPrintable(name));
        }
        const auto group = runCli(m_home.path(), {"help", "editing"});
        QCOMPARE(group.code, 0);
        QVERIFY(group.out.contains("reslice"));
    }

    void unknownCommandFailsWithASuggestion()
    {
        const auto r = runCli(m_home.path(), {"recrod"});
        QVERIFY(r.code != 0);
        QVERIFY(r.err.contains("Unknown command: recrod"));
        QVERIFY(r.err.contains("Did you mean: record?"));

        const auto far = runCli(m_home.path(), {"zzzzzzzzzzzz"});
        QVERIFY(far.code != 0);
        QVERIFY(!far.err.contains("Did you mean"));

        const auto h = runCli(m_home.path(), {"help", "nosuchcommand"});
        QVERIFY(h.code != 0);
    }

    // S3: transcribe / summarize --run local|external|cloud (a per-run route override) and the
    // --local-diarization alias; rejected values fail before anything starts (cloud off in the fixture).
    void runOptionIsParsed()
    {
        const auto help = runCli(m_home.path(), {"help", "transcribe"});
        QVERIFY(help.out.contains("--run local|external|cloud"));
        QVERIFY(help.out.contains("--local-diarization on|off"));
        QVERIFY(runCli(m_home.path(), {"help", "summarize"}).out.contains("--run local|external|cloud"));

        const auto bogus = runCli(m_home.path(), {"transcribe", "nincs-ilyen", "--run", "valahol"});
        QCOMPARE(bogus.code, 1);
        QVERIFY2(bogus.err.contains("--run local|external|cloud"), qPrintable(bogus.err));
        const auto ask = runCli(m_home.path(), {"transcribe", "nincs-ilyen", "--run", "ask"});
        QCOMPARE(ask.code, 1);
        const auto cloud = runCli(m_home.path(), {"transcribe", "nincs-ilyen", "--run", "cloud"});
        QCOMPARE(cloud.code, 1);
        QVERIFY2(cloud.err.contains("Tanara Cloud is not available"), qPrintable(cloud.err));
        const auto external = runCli(m_home.path(), {"summarize", "nincs-ilyen", "--run", "external"});
        QCOMPARE(external.code, 1);
        QVERIFY2(external.err.contains("No external language model provider"), qPrintable(external.err));
        const auto diar = runCli(m_home.path(), {"transcribe", "nincs-ilyen", "--local-diarization", "talan"});
        QCOMPARE(diar.code, 1);
        QVERIFY(diar.err.contains("--local-diarization on|off"));
    }

    void versionSwitchPrintsTheVersion()
    {
        for (const char* sw : {"--version", "-V", "version"}) {
            const auto r = runCli(m_home.path(), {sw});
            QVERIFY2(r.code == 0, sw);
            QVERIFY2(r.out.startsWith("tanara-cli ") && r.out.trimmed().size() > QString("tanara-cli ").size(), sw);
        }
    }

    void noArgumentsPrintTheHelp()
    {
        const auto r = runCli(m_home.path(), {});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("recording"));
    }

private:
    QTemporaryDir m_home;
};

QTEST_GUILESS_MAIN(CliHelpTest)
#include "test_cli_help.moc"
