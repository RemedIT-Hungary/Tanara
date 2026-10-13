// tanara-cli review: the real CLI (QProcess), with an ISOLATED TANARA_HOME, on a made-up
// meeting (no tracks; the embeddings directly in the cache). Checks that the command only
// READS, that the JSON has the core-mismatch group with its evidence, and that no transcript text appears.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"

using namespace tanara;

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

QVector<float> voice(int k)
{
    QVector<float> v(4, 0.0f);
    v[k] = 1.0f;
    return v;
}

} // namespace

class CliReviewTest : public QObject {
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
        QCOMPARE(cli({"review"}).code, 2);
        QCOMPARE(cli({"review", m_home->path(), "--bogus"}).code, 2);
        const Run r = cli({"review", m_home->path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("Not a meeting folder"));
    }

    void readOnlyGroups()
    {
        QTemporaryDir meetingDir;
        const QString folder = meetingDir.path();
        writeJson(QDir(folder).filePath("meeting.json"), QJsonObject{
            {"id", "review-1"}, {"title", "Review"},
            {"speakerMap", QJsonObject{{"A", "Anna"}, {"B", "Béla"}}}});
        // A: 3 confirmed (voice 0), B: 3 confirmed (voice 1), for A 2 lines in B's voice + 1 short.
        const QVector<QPair<QString, int>> spec{{"A", 0}, {"A", 0}, {"A", 0}, {"B", 1}, {"B", 1}, {"B", 1},
                                                {"A", 1}, {"A", 0}, {"A", 1}, {"B", -1}};
        QJsonArray segs;
        for (int i = 0; i < spec.size(); ++i)
            segs.append(QJsonObject{{"startMs", i * 5000}, {"endMs", i * 5000 + (spec[i].second < 0 ? 800 : 4000)},
                                    {"speaker", spec[i].first}, {"text", "titkos szöveg"}});
        writeJson(QDir(folder).filePath("transcript.segments.json"), segs);
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(folder);
        QCOMPARE(lines.size(), spec.size());
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = editing::speakeredit::transcriptFingerprint(lines);
        SpeakerOverlay ov;
        ov.transcriptFingerprint = c.fingerprint;
        for (int i = 0; i < spec.size(); ++i) {
            if (spec[i].second >= 0) c.set("campplus", lines[i].id, voice(spec[i].second));
            if (i < 6) ov.utterances[lines[i].id].confirmed = true;
        }
        QVERIFY(c.save(folder));
        QVERIFY(editing::speakeredit::saveOverlay(folder, ov));

        const QMap<QString, QString> before = snapshot(folder);
        Run r = cli({"review", folder, "--json"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QCOMPARE(snapshot(folder), before);
        QVERIFY(!r.out.contains("titkos"));
        const QJsonObject j = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QVERIFY(!j.value("sidesActive").toBool());
        QCOMPARE(j.value("embeddedLines").toInt(), 9);
        QJsonObject core, shortLines;
        for (const QJsonValue& v : j.value("groups").toArray()) {
            const QJsonObject g = v.toObject();
            if (g.value("kind").toString() == "coreMismatch") core = g;
            if (g.value("kind").toString() == "shortLines") shortLines = g;
        }
        QCOMPARE(core.value("currentSpeakerKey").toString(), QStringLiteral("A"));
        QCOMPARE(core.value("proposedSpeakerKey").toString(), QStringLiteral("B"));
        QCOMPARE(core.value("utteranceIds").toArray().size(), 2);
        QCOMPARE(core.value("confirmedBasis").toInt(), 6);
        QVERIFY(!core.value("evidence").toArray().isEmpty());
        QCOMPARE(shortLines.value("utteranceIds").toArray().size(), 1);
        QCOMPARE(j.value("speakers").toArray().size(), 2);

        r = cli({"review", folder});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("Review groups: 2"));
        QVERIFY(r.out.contains("coreMismatch"));
        QVERIFY(!r.out.contains("titkos"));
        QCOMPARE(snapshot(folder), before);

        // "Cannot be identified": lines ignore, so review skips it, and marks it.
        QCOMPARE(cli({"lines", "ignore", folder}).code, 2);
        QCOMPARE(cli({"lines", "ignore", folder, "u999"}).code, 1);
        QCOMPARE(snapshot(folder), before);
        r = cli({"lines", "ignore", folder, lines[6].id});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QVERIFY(r.out.contains("Marked 1 line(s)"));
        QVERIFY(editing::speakeredit::loadOverlay(folder).utterances.value(lines[6].id).ignored);

        r = cli({"review", folder, "--json"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        const QJsonObject j2 = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(j2.value("ignoredLines").toArray(), QJsonArray{lines[6].id});
        for (const QJsonValue& v : j2.value("groups").toArray())
            QVERIFY(!v.toObject().value("utteranceIds").toArray().contains(lines[6].id));
        r = cli({"review", folder});
        QVERIFY(r.out.contains("Not identifiable (ignored by voice analysis): 1"));

        // The line listing: markers, without text; read-only.
        const QMap<QString, QString> beforeList = snapshot(folder);
        r = cli({"lines", folder, "--json"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QCOMPARE(snapshot(folder), beforeList);
        QVERIFY(!r.out.contains("titkos"));
        const QJsonObject lj = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(lj.value("ignored").toInt(), 1);
        const QJsonArray la = lj.value("lines").toArray();
        QCOMPARE(la.size(), lines.size());
        QVERIFY(la.at(6).toObject().value("ignored").toBool());
        QVERIFY(la.at(0).toObject().value("confirmed").toBool());
        QVERIFY(!la.at(7).toObject().value("ignored").toBool());
        QCOMPARE(la.at(0).toObject().value("speaker").toString(), QStringLiteral("Anna"));
        r = cli({"lines", folder});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("not identifiable (ignored): 1"));
        QVERIFY(r.out.contains("ignored"));
        QVERIFY(!r.out.contains("titkos"));

        r = cli({"lines", "unignore", folder, lines[6].id});
        QCOMPARE(r.code, 0);
        QVERIFY(!editing::speakeredit::loadOverlay(folder).utterances.value(lines[6].id).ignored);
        QVERIFY(editing::speakeredit::loadOverlay(folder).utterances.value(lines[0].id).confirmed);
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

QTEST_GUILESS_MAIN(CliReviewTest)
#include "test_cli_review.moc"
