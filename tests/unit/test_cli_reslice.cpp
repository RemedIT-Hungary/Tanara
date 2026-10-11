//
// tanara-cli reslice: a valódi CLI (QProcess), IZOLÁLT TANARA_HOME-mal, kitalált megbeszélésen
// (szintetikus Soniox-féle tokens.json + segments.json + overlay + summary.json). Ellenőrzi:
// --dry-run nem ír; az írás után az id-k átmentek (overlay-javítások, megerősítések, noisy,
// summary-források), az ujjlenyomat stimmel, a .bak egyszer készül; a második futás és a
// valódi szó-időzítés „nothing to do". Átirat-szöveg nem kerül a kimenetre.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/TranscriptMerger.h"
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
        QFile f(fi.filePath());
        f.open(QIODevice::ReadOnly);
        m.insert(fi.fileName(), QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex()));
    }
    return m;
}

void writeBytes(const QString& path, const QByteArray& b)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(b);
}

void writeJson(const QString& path, const QJsonValue& v)
{
    writeBytes(path, v.isArray() ? QJsonDocument(v.toArray()).toJson() : QJsonDocument(v.toObject()).toJson());
}

QJsonObject readObject(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(f.readAll()).object();
}

// (szöveg, beszélő, sáv, kezdet, vég)
struct Tok { const char* text; const char* speaker; const char* track; qint64 s; qint64 e; };

// Egy meeting-mappa a megadott tokenekből: tokens.json + segments.json (a mergerrel, ahogy az
// átírás írja) + meeting.json + transcript.md.
void writeMeeting(const QString& folder, const QVector<Tok>& toks)
{
    writeJson(QDir(folder).filePath("meeting.json"), QJsonObject{
        {"id", "reslice-1"}, {"title", "Reslice"}, {"speakerMap", QJsonObject{{"A", "Anna"}, {"B", "Béla"}}}});
    QVector<TrackTranscript> tracks;
    for (const char* id : {"t1", "t2"}) {
        TrackTranscript tt;
        tt.trackId = QString::fromLatin1(id);
        for (const Tok& t : toks)
            if (tt.trackId == QLatin1String(t.track))
                tt.tokens.append(TranscriptToken{QString::fromUtf8(t.text), QString::fromLatin1(t.speaker), t.s, t.e, 0.9, tt.trackId});
        tracks.append(tt);
    }
    MergedTranscript mt = mergeTranscripts(tracks);
    mt.language = "hu";
    QJsonArray arr;
    for (const TranscriptToken& t : mt.tokens)
        arr.append(QJsonObject{{"text", t.text}, {"speaker", t.speaker}, {"startMs", double(t.startMs)},
                               {"endMs", double(t.endMs)}, {"confidence", t.confidence}, {"trackId", t.trackId}});
    writeBytes(QDir(folder).filePath("transcript.tokens.json"),
               QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", arr}}).toJson(QJsonDocument::Compact));
    QJsonArray segs;
    for (const Utterance& u : mt.segments())
        segs.append(QJsonObject{{"startMs", double(u.startMs)}, {"endMs", double(u.endMs)},
                                {"speaker", u.speaker}, {"text", u.text}});
    writeJson(QDir(folder).filePath("transcript.segments.json"), segs);
    writeBytes(QDir(folder).filePath("transcript.md"), "`[00:01]` **Anna** régi\n");
}

const QVector<Tok> kSoniox{
    {" Titkos", "A", "t1", 1000, 1060}, {" szö", "A", "t1", 1120, 1180}, {"veg.", "A", "t1", 1240, 1300},
    {" Másik", "B", "t2", 3000, 3060}, {" sor.", "B", "t2", 3120, 3180},
    {" Harmadik", "A", "t1", 6000, 6060}, {" sor.", "A", "t1", 6120, 6180},
    {" Rövid.", "B", "t2", 9000, 9060},
    {" Egyszerre", "A", "t1", 12000, 12060}, {" Egyszerre", "B", "t2", 12000, 12060},
};

} // namespace

class CliResliceTest : public QObject {
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
        QCOMPARE(cli({"reslice"}).code, 2);
        QCOMPARE(cli({"reslice", m_home->path(), "--bogus"}).code, 2);
        const Run r = cli({"reslice", m_home->path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("transcript.tokens.json"));
    }

    void realWordSpansAreLeftAlone()
    {
        QTemporaryDir dir;
        writeMeeting(dir.path(), {{" Egy", "A", "t1", 0, 320}, {" kettő", "A", "t1", 320, 700},
                                  {" három", "B", "t2", 2000, 2450}});
        const auto before = snapshot(dir.path());
        const Run r = cli({"reslice", dir.path()});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("timing looks like real word spans; nothing to do"), qPrintable(r.out));
        QCOMPARE(snapshot(dir.path()), before);
    }

    void dryRunThenResliceRemapsIds()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        writeMeeting(folder, kSoniox);

        // Régi id-k: u1000, u3000, u6000, u9000, u12000 (A), u12000-2 (B).
        const QVector<TranscriptLine> old = speakeredit::loadTranscriptLines(folder);
        QCOMPARE(old.size(), 6);
        QCOMPARE(old.at(4).id, QStringLiteral("u12000"));
        QCOMPARE(old.at(5).id, QStringLiteral("u12000-2"));

        SpeakerOverlay ov;
        ov.transcriptFingerprint = speakeredit::transcriptFingerprint(old);
        ov.utterances["u1000"].confirmed = true;
        ov.utterances["u9000"].speaker = "A";
        ov.utterances["u9000"].corrected = true;
        ov.utterances["u12000-2"].noisy = true;
        ov.utterances["u6000"].rechecked = true;
        ov.utterances["u6000"].recheckHint = "B";
        ov.speakerTracks["A"] = QStringList{"t1"};
        QVERIFY(speakeredit::saveOverlay(folder, ov));

        writeJson(QDir(folder).filePath("summary.json"), QJsonObject{
            {"version", 4}, {"mode", "quick"},
            {"summary", QJsonObject{
                {"execSummary", "x"},
                {"statements", QJsonArray{QJsonObject{
                    {"id", "s1"}, {"kind", "statement"}, {"text", "y"},
                    {"sourceSpans", QJsonArray{
                        QJsonObject{{"startMs", 1000}, {"endMs", 1300}, {"utteranceIds", QJsonArray{"u1000"}}},
                        QJsonObject{{"startMs", 12000}, {"endMs", 12060}, {"utteranceIds", QJsonArray{"u12000-2"}}}}}}}},
                {"sourceSpeakers", QJsonObject{{"u1000", QJsonObject{{"key", "A"}, {"name", "Anna"}}},
                                               {"u12000-2", QJsonObject{{"key", "B"}, {"name", "Béla"}}}}}}}});
        writeBytes(UtteranceEmbeddingCache::filePath(folder), "stale-cache");

        // 1) --dry-run: semmi nem változik, a számok a JSON-ban.
        const auto before = snapshot(folder);
        Run r = cli({"reslice", folder, "--dry-run", "--json"});
        QCOMPARE(r.code, 0);
        QCOMPARE(snapshot(folder), before);
        const QJsonObject dj = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(dj.value("verdict").toString(), QStringLiteral("reslice"));
        QCOMPARE(dj.value("medianTokenWidthMs").toInt(), 60);
        QCOMPARE(dj.value("lines").toObject().value("old").toInt(), 6);
        QCOMPARE(dj.value("lines").toObject().value("new").toInt(), 6);
        QCOMPARE(dj.value("lines").toObject().value("renamedIds").toInt(), 6);
        QCOMPARE(dj.value("lines").toObject().value("mergedLines").toInt(), 0);
        QCOMPARE(dj.value("lines").toObject().value("textChanged").toInt(), 0);
        QCOMPARE(dj.value("startShiftHistogramMs").toObject().value("201-300").toInt(), 6);
        QCOMPARE(dj.value("overlay").toObject().value("renamed").toInt(), 4);
        QCOMPARE(dj.value("overlay").toObject().value("conflicts").toInt(), 0);
        QCOMPARE(dj.value("summary").toObject().value("renamed").toInt(), 4);
        QVERIFY(!r.out.contains("Titkos") && !r.out.contains("Egyszerre"));

        // Szöveges dry-run is olvasható és nem ír.
        r = cli({"reslice", folder, "--dry-run"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("Dry run: nothing written."));
        QVERIFY(!r.out.contains("Titkos"));
        QCOMPARE(snapshot(folder), before);

        // 2) írás.
        r = cli({"reslice", folder});
        QVERIFY2(r.code == 0, qPrintable(r.err + r.out));
        QVERIFY(!r.out.contains("Titkos"));
        for (const char* bak : {"transcript.tokens.json.bak", "transcript.segments.json.bak",
                                "transcript.speakers.json.bak", "summary.json.bak", "transcript.md.bak"})
            QVERIFY2(QFile::exists(QDir(folder).filePath(bak)), bak);
        QCOMPARE(snapshot(folder).value("transcript.segments.json.bak"), before.value("transcript.segments.json"));
        QVERIFY(!QFile::exists(UtteranceEmbeddingCache::filePath(folder)));

        const QVector<TranscriptLine> now = speakeredit::loadTranscriptLines(folder);
        QStringList ids;
        for (const TranscriptLine& l : now) ids << l.id;
        QCOMPARE(ids, (QStringList{"u700", "u2700", "u5700", "u8700", "u11700", "u11700-2"}));
        QCOMPARE(now.at(0).endMs, 1400);
        QCOMPARE(now.at(3).endMs, 9160);
        QCOMPARE(now.at(0).text, old.at(0).text);

        // Az overlay az új átirathoz validálva is megtartja a javításokat.
        const SpeakerOverlay nov = speakeredit::loadOverlayFor(folder, now);
        QCOMPARE(nov.utterances.size(), 4);
        QVERIFY(nov.utterances.value("u700").confirmed);
        QCOMPARE(nov.utterances.value("u8700").speaker, QStringLiteral("A"));
        QVERIFY(nov.utterances.value("u8700").corrected);
        QCOMPARE(nov.utterances.value("u11700-2").noisy, std::optional<bool>(true));
        QVERIFY(nov.utterances.value("u5700").rechecked);
        QCOMPARE(nov.utterances.value("u5700").recheckHint, QStringLiteral("B"));
        QCOMPARE(nov.speakerTracks.value("A"), QStringList{"t1"});
        QCOMPARE(speakeredit::resolveSpeakerKey(nov, now.at(3)), QStringLiteral("A"));

        const QJsonObject sum = readObject(QDir(folder).filePath("summary.json")).value("summary").toObject();
        const QJsonArray spans = sum.value("statements").toArray().at(0).toObject().value("sourceSpans").toArray();
        QCOMPARE(spans.at(0).toObject().value("utteranceIds").toArray().at(0).toString(), QStringLiteral("u700"));
        QCOMPARE(spans.at(0).toObject().value("startMs").toInt(), 700);
        QCOMPARE(spans.at(0).toObject().value("endMs").toInt(), 1400);
        QCOMPARE(spans.at(1).toObject().value("utteranceIds").toArray().at(0).toString(), QStringLiteral("u11700-2"));
        QCOMPARE(spans.at(1).toObject().value("startMs").toInt(), 11700);
        QVERIFY(sum.value("sourceSpeakers").toObject().contains("u700"));
        QVERIFY(sum.value("sourceSpeakers").toObject().contains("u11700-2"));
        QVERIFY(!sum.value("sourceSpeakers").toObject().contains("u1000"));

        const QJsonObject tokRoot = readObject(QDir(folder).filePath("transcript.tokens.json"));
        QCOMPARE(tokRoot.value("timing").toString(), QStringLiteral("soniox-corrected"));
        QCOMPARE(tokRoot.value("language").toString(), QStringLiteral("hu"));
        QCOMPARE(tokRoot.value("tokens").toArray().size(), kSoniox.size());

        QFile md(QDir(folder).filePath("transcript.md"));
        QVERIFY(md.open(QIODevice::ReadOnly));
        QVERIFY(md.readAll().startsWith("`[00:00]` **Anna**"));

        // 3) második futás: már korrigált; a .bak nem íródik felül.
        const auto after = snapshot(folder);
        r = cli({"reslice", folder});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("already corrected; nothing to do"));
        QCOMPARE(snapshot(folder), after);
    }

    // Egy .bak már létezik (korábbi kézi mentés): nem írjuk felül.
    void existingBackupIsKept()
    {
        QTemporaryDir dir;
        writeMeeting(dir.path(), kSoniox);
        writeBytes(QDir(dir.path()).filePath("transcript.segments.json.bak"), "earlier");
        const Run r = cli({"reslice", dir.path()});
        QCOMPARE(r.code, 0);
        QFile f(QDir(dir.path()).filePath("transcript.segments.json.bak"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("earlier"));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath("transcript.tokens.json.bak")));
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

QTEST_GUILESS_MAIN(CliResliceTest)
#include "test_cli_reslice.moc"
