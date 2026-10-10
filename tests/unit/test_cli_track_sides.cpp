//
// tanara-cli track-sides: a valódi CLI (QProcess), IZOLÁLT TANARA_HOME-mal, kitalált
// megbeszélésen (ffmpeg lavfi-generált mic- és loopback-sáv). Ellenőrzi, hogy a parancs CSAK
// OLVAS (a mappa változatlan), a --write-cache írja a tracks.activity.bin-t, és a következő
// futás abból dolgozik. Nincs ffmpeg → QSKIP.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

struct Run { int code = -1; QString out; QString err; };

QMap<QString, QString> snapshot(const QString& dir)
{
    QMap<QString, QString> m;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi(it.next());
        m.insert(fi.filePath(), QStringLiteral("%1/%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()));
    }
    return m;
}

void writeJson(const QString& path, const QJsonValue& v)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(v.isArray() ? QJsonDocument(v.toArray()).toJson() : QJsonDocument(v.toObject()).toJson());
}

bool generate(const QString& expr, const QString& out)
{
    QProcess ff;
    ff.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", expr + ":s=16000:d=8", "-y", out});
    return ff.waitForFinished(30000) && ff.exitCode() == 0 && QFileInfo::exists(out);
}

} // namespace

class CliTrackSidesTest : public QObject {
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
        QCOMPARE(cli({"track-sides"}).code, 2);
        QCOMPARE(cli({"track-sides", m_home->path(), "--bogus"}).code, 2);
        QCOMPARE(cli({"track-sides", m_home->path(), "--frame-ms", "0"}).code, 2);
        const Run r = cli({"track-sides", m_home->path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("Not a meeting folder"));
    }

    void readOnlyAnalysisAndCache()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) QSKIP("nincs ffmpeg");
        QTemporaryDir meetingDir;
        const QString folder = meetingDir.path();
        // 0–4 mp: csak a mikrofon szól; 4–8 mp: csak a loopback.
        QVERIFY(generate("aevalsrc=if(lt(t\\,4)\\,0.5*sin(2*PI*220*t)\\,0)", QDir(folder).filePath("track_mic.wav")));
        QVERIFY(generate("aevalsrc=if(gte(t\\,4)\\,0.3*sin(2*PI*330*t)\\,0)", QDir(folder).filePath("track_loop.wav")));
        writeJson(QDir(folder).filePath("meeting.json"), QJsonObject{
            {"id", "sides-1"}, {"title", "Sides"},
            {"speakerMap", QJsonObject{{"Beszélő 1", "Tester"}}},
            {"tracks", QJsonArray{
                QJsonObject{{"id", "mic"}, {"file", "track_mic.wav"}, {"kind", "mic"}, {"deviceName", "Mic"},
                            {"fixedSpeaker", true}, {"speakerLabel", "Tester"}, {"active", true}},
                QJsonObject{{"id", "loop"}, {"file", "track_loop.wav"}, {"kind", "loopback"},
                            {"deviceName", "Monitor of Out"}, {"active", true}}}}});
        QJsonArray segs;
        for (int i = 0; i < 4; ++i)
            segs.append(QJsonObject{{"startMs", i * 2000}, {"endMs", i * 2000 + 1900},
                                    {"speaker", "Beszélő 1"}, {"text", "titkos szöveg"}});
        writeJson(QDir(folder).filePath("transcript.segments.json"), segs);

        const QMap<QString, QString> before = snapshot(folder);
        Run r = cli({"track-sides", folder, "--json"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QCOMPARE(snapshot(folder), before);                      // semmit nem írt a mappába
        QVERIFY(!r.out.contains("titkos"));                      // átirat-szöveg nincs a kimenetben

        QJsonObject j = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QVERIFY(j.value("active").toBool());
        QVERIFY(!j.value("activityFromCache").toBool());
        QCOMPARE(j.value("tracks").toArray().size(), 2);
        const QJsonObject totals = j.value("totals").toObject();
        QCOMPARE(totals.value("local").toInt(), 2);
        QCOMPARE(totals.value("remote").toInt(), 2);
        const QJsonObject raw = j.value("rawLabels").toObject().value(u"Beszélő 1"_s).toObject();
        QCOMPARE(raw.value("local").toInt(), 2);
        QCOMPARE(raw.value("remote").toInt(), 2);
        const QJsonObject p = j.value("persons").toArray().at(0).toObject();
        QCOMPARE(p.value("name").toString(), QStringLiteral("Tester"));
        QCOMPARE(p.value("side").toString(), QStringLiteral("local"));
        QCOMPARE(p.value("basis").toString(), QStringLiteral("fixed-track"));
        QCOMPARE(p.value("conflicts").toInt(), 2);
        QCOMPARE(j.value("conflicts").toArray().size(), 2);

        // Szöveges kimenet.
        r = cli({"track-sides", folder});
        QCOMPARE(r.code, 0);
        QVERIFY(!r.out.contains("localShare histogram"));
        QVERIFY(r.out.contains("Conflicts: 2"));
        r = cli({"track-sides", folder, "--legacy"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("localShare histogram"));
        QVERIFY(r.out.contains("Legacy lines: local 2  remote 2"));
        r = cli({"track-sides", folder, "--json", "--legacy"});
        j = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(j.value("legacy").toObject().value("totals").toObject().value("remote").toInt(), 2);
        QCOMPARE(j.value("lines").toArray().at(0).toObject().value("legacySide").toString(), QStringLiteral("local"));
        QCOMPARE(snapshot(folder), before);

        // --write-cache: létrejön a cache, a következő futás abból számol.
        r = cli({"track-sides", folder, "--write-cache", "--json"});
        QCOMPARE(r.code, 0);
        QVERIFY(QFileInfo::exists(QDir(folder).filePath("tracks.activity.bin")));
        r = cli({"track-sides", folder, "--json"});
        j = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QVERIFY(j.value("activityFromCache").toBool());
        QCOMPARE(j.value("conflicts").toArray().size(), 2);
        r = cli({"track-sides", folder, "--json", "--no-cache"});
        QVERIFY(!QJsonDocument::fromJson(r.out.toUtf8()).object().value("activityFromCache").toBool());
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

QTEST_GUILESS_MAIN(CliTrackSidesTest)
#include "test_cli_track_sides.moc"
