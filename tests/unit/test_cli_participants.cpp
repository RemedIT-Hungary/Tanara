// tanara-cli participants <meeting-folder>: the real CLI (QProcess), with an ISOLATED
// TANARA_HOME, a made-up meeting.json. Checks the argument errors, the list's JSON (group,
// default check, raw speakers), that it only READS by default, and that without a model
// --analyze stops with an error (writes nothing).
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

struct Run { int code = -1; QString out; QString err; };

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class CliParticipantsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
    }
    void cleanup() { m_home.reset(); }

    void argumentErrors()
    {
        QTemporaryDir dir;
        QCOMPARE(cli({"participants", dir.path(), "--bogus"}).code, 2);
        QCOMPARE(cli({"participants", dir.path(), "--write"}).code, 2);   // --write only with --analyze
        const Run r = cli({"participants", dir.path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("Not a meeting folder"));
    }

    void listsCandidatesReadOnly()
    {
        QTemporaryDir dir;
        const QString meetingJson = QDir(dir.path()).filePath("meeting.json");
        const QJsonObject voice{{"kind", "voice"}, {"polarity", "support"}, {"value", 0.82}, {"text", "hang 82%"},
                                {"fixTarget", "samples"}};
        const QJsonObject side{{"kind", "side"}, {"polarity", "support"}, {"value", 1.0}, {"text", "mikrofon"}};
        QFile f(meetingJson);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{
            {"id", "p-1"}, {"title", "P"},
            {"tracks", QJsonArray{QJsonObject{{"id", "mic"}, {"file", "track_mic.ogg"}, {"kind", "mic"},
                                              {"active", true}, {"speechRatio", 0.4}},
                                  QJsonObject{{"id", "loop"}, {"file", "track_loop.ogg"}, {"kind", "loopback"},
                                              {"active", false}, {"excludedReason", "noSpeech"}, {"speechRatio", 0.0}}}},
            {"participants", QJsonArray{
                QJsonObject{{"id", "v1"}, {"personName", "Tester"}, {"source", "voice"}, {"sides", QJsonArray{"mic"}},
                            {"rawSpeakerIds", QJsonArray{"Beszélő 1@mic"}}, {"evidence", QJsonArray{voice, side}},
                            {"talkShare", 0.6}},
                QJsonObject{{"id", "v2"}, {"personName", ""}, {"source", "voice"}, {"sides", QJsonArray{"loopback"}},
                            {"evidence", QJsonArray{QJsonObject{{"kind", "voice"}, {"polarity", "neutral"},
                                                                {"text", "ismeretlen hang"}}}}}}}}).toJson());
        f.close();
        const QByteArray before = readAll(meetingJson);

        const Run r = cli({"participants", dir.path(), "--json"});
        QCOMPARE(r.code, 0);
        const QJsonObject root = QJsonDocument::fromJson(r.out.toUtf8()).object();
        const QJsonArray ps = root.value("participants").toArray();
        QCOMPARE(ps.size(), 2);
        QCOMPARE(ps[0].toObject().value("group").toString(), QStringLiteral("sure"));
        QVERIFY(ps[0].toObject().value("defaultChecked").toBool());
        QCOMPARE(ps[1].toObject().value("group").toString(), QStringLiteral("doubt"));
        QVERIFY(!ps[1].toObject().value("defaultChecked").toBool());
        const QJsonArray tracks = root.value("tracks").toArray();
        QCOMPARE(tracks[1].toObject().value("excludedReason").toString(), QStringLiteral("noSpeech"));
        QVERIFY(!tracks[1].toObject().value("included").toBool());

        const Run t = cli({"participants", dir.path()});
        QCOMPARE(t.code, 0);
        QVERIFY(t.out.contains("SURE (1)"));
        QVERIFY(t.out.contains("Beszélő 1 · microphone"));
        QVERIFY(t.out.contains("excluded (noSpeech)"));
        QCOMPARE(readAll(meetingJson), before);   // reads only

        // Without a model (empty sandbox) the analysis does not run, and does not write.
        const Run a = cli({"participants", dir.path(), "--analyze", "--write"});
        QCOMPARE(a.code, 1);
        QVERIFY(a.err.contains("No usable voice model"));
        QCOMPARE(readAll(meetingJson), before);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath("participants.analysis.json")));
    }

    // --remove: permanent deletion from meeting.json (together with the approval entry and the
    // raw speaker mapped to its name).
    void removeParticipant()
    {
        QTemporaryDir dir;
        const QString meetingJson = QDir(dir.path()).filePath("meeting.json");
        QFile f(meetingJson);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{
            {"id", "p-2"}, {"title", "P"},
            {"speakerMap", QJsonObject{{"Beszélő 1", "Tester"}, {"Beszélő 2", "Other"}}},
            {"participants", QJsonArray{
                QJsonObject{{"id", "v1"}, {"personName", "Tester"}, {"source", "voice"},
                            {"rawSpeakerIds", QJsonArray{"Beszélő 1@mic"}}, {"approved", true}},
                QJsonObject{{"id", "m-1"}, {"personName", "Other"}, {"source", "manual"}}}},
            {"participantApproval", QJsonObject{{"at", "2026-10-11T10:00:00"},
                {"candidates", QJsonArray{QJsonObject{{"participantId", "v1"}, {"personName", "Tester"}, {"checked", true}},
                                          QJsonObject{{"participantId", "m-1"}, {"personName", "Other"}}}}}}}).toJson());
        f.close();

        QCOMPARE(cli({"participants", dir.path(), "--remove", "nincs"}).code, 1);
        QCOMPARE(cli({"participants", dir.path(), "--remove", "v1", "--analyze"}).code, 2);
        const Run r = cli({"participants", dir.path(), "--remove", "v1", "--json"});
        QCOMPARE(r.code, 0);
        const QJsonObject res = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(res.value("removed").toString(), QStringLiteral("v1"));
        QVERIFY(res.value("unmappedSpeakers").toBool());

        QFile in(meetingJson);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QJsonObject m = QJsonDocument::fromJson(in.readAll()).object();
        const QJsonArray ps = m.value("participants").toArray();
        QCOMPARE(ps.size(), 1);
        QCOMPARE(ps[0].toObject().value("id").toString(), QStringLiteral("m-1"));
        const QJsonArray cand = m.value("participantApproval").toObject().value("candidates").toArray();
        QCOMPARE(cand.size(), 1);
        QVERIFY(!m.value("speakerMap").toObject().contains("Beszélő 1"));
        QCOMPARE(m.value("speakerMap").toObject().value("Beszélő 2").toString(), QStringLiteral("Other"));
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

QTEST_GUILESS_MAIN(CliParticipantsTest)
#include "test_cli_participants.moc"
