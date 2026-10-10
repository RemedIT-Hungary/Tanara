//
// tanara-cli review: a valódi CLI (QProcess), IZOLÁLT TANARA_HOME-mal, kitalált megbeszélésen
// (sávok nélkül; a beágyazások közvetlenül a cache-ben). Ellenőrzi, hogy a parancs CSAK OLVAS,
// a JSON-ban ott a mag-eltérés csoport a bizonyítékaival, és átirat-szöveg nem jelenik meg.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"

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
        // A: 3 megerősített (0. hang), B: 3 megerősített (1. hang), A-nál 2 sor B hangján + 1 rövid.
        const QVector<QPair<QString, int>> spec{{"A", 0}, {"A", 0}, {"A", 0}, {"B", 1}, {"B", 1}, {"B", 1},
                                                {"A", 1}, {"A", 0}, {"A", 1}, {"B", -1}};
        QJsonArray segs;
        for (int i = 0; i < spec.size(); ++i)
            segs.append(QJsonObject{{"startMs", i * 5000}, {"endMs", i * 5000 + (spec[i].second < 0 ? 800 : 4000)},
                                    {"speaker", spec[i].first}, {"text", "titkos szöveg"}});
        writeJson(QDir(folder).filePath("transcript.segments.json"), segs);
        const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(folder);
        QCOMPARE(lines.size(), spec.size());
        UtteranceEmbeddingCache c;
        c.fingerprint = speakeredit::transcriptFingerprint(lines);
        SpeakerOverlay ov;
        ov.transcriptFingerprint = c.fingerprint;
        for (int i = 0; i < spec.size(); ++i) {
            if (spec[i].second >= 0) c.set("campplus", lines[i].id, voice(spec[i].second));
            if (i < 6) ov.utterances[lines[i].id].confirmed = true;
        }
        QVERIFY(c.save(folder));
        QVERIFY(speakeredit::saveOverlay(folder, ov));

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
