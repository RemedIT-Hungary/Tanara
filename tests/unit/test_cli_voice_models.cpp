//
// tanara-cli voice-models / voice-eval: a parancsok a valódi CLI-n (QProcess), IZOLÁLT
// TANARA_HOME-mal. Hálózatot nem használ (a fetch csak a „már létezik” ágon fut). A voice-eval
// végig-próbája a valódi CAM++ modellel megy (csak olvasva, a homokozóba linkelve) — ha nincs meg,
// vagy nincs ffmpeg, QSKIP. A voice-eval nem írhat a meeting-mappába: ezt is ellenőrizzük.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

using namespace tanara;

namespace {

struct Run { int code = -1; QString out; QString err; };

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// A mappa pillanatképe: név → méret + módosítási idő.
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

} // namespace

class CliVoiceModelsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
    }
    void cleanup() { m_home.reset(); }

    void listEnableDisable()
    {
        Run r = cli({"voice-models"});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("ID") && r.out.contains("LICENSE"), qPrintable(r.out));
        for (const VoiceModelSpec& s : VoiceModelRegistry::builtin()) {
            QVERIFY(r.out.contains(s.id));
            QVERIFY(r.out.contains(s.license));
        }
        // Alapból csak a campplus be; fájl sehol (a homokozóban nincs modell).
        const QStringList lines = r.out.split('\n');
        for (const QString& l : lines) {
            if (l.startsWith("campplus ")) QVERIFY(l.contains(" yes "));
            if (l.startsWith("wespeaker-resnet34-lm ")) QVERIFY(l.contains(" no "));
        }

        r = cli({"voice-models", "enable", "wespeaker-resnet34-lm"});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("model file is missing"));
        QCOMPARE(settingsModels(), QStringList({"campplus", "wespeaker-resnet34-lm"}));

        r = cli({"voice-models", "disable", "campplus"});
        QCOMPARE(r.code, 0);
        QCOMPARE(settingsModels(), QStringList({"wespeaker-resnet34-lm"}));

        r = cli({"voice-models", "enable", "nincs-ilyen"});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("Unknown voice model"));
        QCOMPARE(settingsModels(), QStringList({"wespeaker-resnet34-lm"}));

        // A meglévő fájl helye a listában.
        touch(modelFile("wespeaker-resnet34-lm"));
        r = cli({"voice-models", "list"});
        QVERIFY(r.out.contains(QDir::toNativeSeparators(modelFile("wespeaker-resnet34-lm"))));
        QCOMPARE(cli({"voice-models", "bogus"}).code, 2);
    }

    void fetchRefusesExistingFile()
    {
        const QString target = modelFile("eres2netv2");
        touch(target);
        const qint64 size = QFileInfo(target).size();
        const Run r = cli({"voice-models", "fetch", "eres2netv2"});
        QCOMPARE(r.code, 1);
        QVERIFY(r.out.contains("Apache-2.0"));                    // a licenc a letöltés előtt
        QVERIFY(r.out.contains("modelscope.cn"));
        QVERIFY(r.err.contains("--force"));
        QCOMPARE(QFileInfo(target).size(), size);
        QCOMPARE(cli({"voice-models", "fetch", "nincs-ilyen"}).code, 1);
        QCOMPARE(cli({"voice-models", "fetch"}).code, 2);
    }

    void voiceEvalArgumentErrors()
    {
        QCOMPARE(cli({"voice-eval"}).code, 2);
        // Nincs modellfájl a homokozóban.
        Run r = cli({"voice-eval", m_home->path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("Model file missing"));
        QCOMPARE(cli({"voice-eval", m_home->path(), "--models", "nincs-ilyen"}).code, 1);
    }

    void voiceEvalRealModelIsReadOnly()
    {
        const QString real = QDir::home().filePath(".tanara/models/campplus_sv_zh_en_16k.onnx");
        if (!QFileInfo::exists(real)) QSKIP("a CAM++ modell nincs meg (~/.tanara/models)");
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) QSKIP("nincs ffmpeg");
        QDir().mkpath(QFileInfo(modelFile("campplus")).absolutePath());
        QVERIFY(QFile::link(real, modelFile("campplus")));   // csak olvasva használjuk

        // Kitalált megbeszélés: A (200 Hz) és B (700+1300 Hz) 4 mp-enként váltakozik.
        QTemporaryDir meetingDir;
        const QString folder = meetingDir.path();
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                            "aevalsrc=if(lt(mod(t\\,8)\\,4)\\,0.5*sin(2*PI*200*t)\\,"
                            "0.3*sin(2*PI*700*t)+0.2*sin(2*PI*1300*t)):s=16000:d=24",
                            "-y", QDir(folder).filePath("mixdown.wav")});
        QVERIFY(ff.waitForFinished(30000));
        QVERIFY(QFileInfo::exists(QDir(folder).filePath("mixdown.wav")));
        writeJson(QDir(folder).filePath("meeting.json"),
                  QJsonObject{{"id", "eval-1"}, {"title", "Eval"}, {"mixdownFile", "mixdown.wav"}});
        QJsonArray segs;
        for (int i = 0; i < 6; ++i)
            segs.append(QJsonObject{{"startMs", i * 4000}, {"endMs", i * 4000 + 3800},
                                    {"speaker", i % 2 == 0 ? "Beszélő 1" : "Beszélő 2"}, {"text", "x"}});
        writeJson(QDir(folder).filePath("transcript.segments.json"), segs);
        const auto lines = speakeredit::loadTranscriptLines(folder);
        QCOMPARE(lines.size(), 6);
        SpeakerOverlay ov;
        ov.transcriptFingerprint = speakeredit::transcriptFingerprint(lines);
        for (const auto& l : lines) ov.utterances[l.id].confirmed = true;
        QVERIFY(speakeredit::saveOverlay(folder, ov));

        const QMap<QString, QString> before = snapshot(folder);
        const Run r = cli({"voice-eval", folder, "--json", "--min-ms", "1500"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QCOMPARE(snapshot(folder), before);                      // semmit nem írt a mappába

        const QJsonObject j = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(j.value("models").toArray(), QJsonArray({"campplus"}));
        const QJsonArray spaces = j.value("spaces").toArray();
        QCOMPARE(spaces.size(), 1);                              // egy modell: nincs fúziós tér
        const QJsonObject sp = spaces.first().toObject();
        QCOMPARE(sp.value("linesCovered").toInt(), 6);
        QCOMPARE(j.value("speakers").toArray().size(), 2);
        QCOMPARE(sp.value("coreLines").toArray(), QJsonArray({3, 3}));
        QVERIFY(sp.value("coreCosine").toArray().at(0).toArray().at(1).isDouble());

        // Szöveges kimenet is.
        const Run t = cli({"voice-eval", folder});
        QCOMPARE(t.code, 0);
        QVERIFY(t.out.contains("== campplus =="));
        QVERIFY(t.out.contains("(4) lines closer to another core"));
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
    QStringList settingsModels() const
    {
        QStringList out;
        for (const QJsonValue& v : readJson(QDir(m_home->path()).filePath("settings.json")).value("voiceModels").toArray())
            out << v.toString();
        return out;
    }
    QString modelFile(const QString& id) const
    {
        return QDir(m_home->path()).filePath("models/" + VoiceModelRegistry::spec(id)->fileName);
    }
    static void touch(const QString& path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write("x");
    }
    static void writeJson(const QString& path, const QJsonValue& v)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(v.isArray() ? QJsonDocument(v.toArray()).toJson() : QJsonDocument(v.toObject()).toJson());
    }

    std::unique_ptr<QTemporaryDir> m_home;
};

QTEST_GUILESS_MAIN(CliVoiceModelsTest)
#include "test_cli_voice_models.moc"
