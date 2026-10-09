//
// Validációs harness a beszélő-szerkesztő hang-elemzéséhez VALÓDI (eldobható) adatokon:
// embedding-sebesség, a bizonytalannak jelölt sorok aránya, és a „hasonló sorok" javaslat
// pontossága. Alapból KIHAGYVA — csak a TANARA_SANDBOX környezeti változóval fut:
//
//   TANARA_SANDBOX=<mappa> ./build/tests/test_speaker_sandbox
//
// ahol <mappa>/home a metaadat-mappa (benne models/campplus_sv_zh_en_16k.onnx) és
// <mappa>/recordings a meetingek mappája. SOHA ne mutasson a valódi ~/.tanara-ra: a futás
// ír a meeting-mappákba (transcript.embeddings.bin). Átirat-szöveget nem ír ki.
//
#include <QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <algorithm>
#include <cmath>

using namespace tanara;
using namespace tanara::speakeredit;

namespace {

double quantile(QVector<double> v, double q)
{
    if (v.isEmpty()) return qQNaN();
    std::sort(v.begin(), v.end());
    return v[int(std::clamp(q * (v.size() - 1), 0.0, double(v.size() - 1)))];
}

QString dist(const QVector<double>& v)
{
    return QStringLiteral("n=%1 p10=%2 p50=%3 p90=%4").arg(v.size())
        .arg(quantile(v, 0.1), 0, 'f', 2).arg(quantile(v, 0.5), 0, 'f', 2)
        .arg(quantile(v, 0.9), 0, 'f', 2);
}

} // namespace

class SpeakerSandboxTest : public QObject {
    Q_OBJECT
private slots:
    void measure();
};

void SpeakerSandboxTest::measure()
{
    const QString root = qEnvironmentVariable("TANARA_SANDBOX");
    if (root.isEmpty()) QSKIP("TANARA_SANDBOX nincs beállítva — a valós-adatos mérés kihagyva.");
    const QString home = QDir(root).filePath(QStringLiteral("home"));
    const QString rec = QDir(root).filePath(QStringLiteral("recordings"));
    const QString model = QDir(home).filePath(QStringLiteral("models/campplus_sv_zh_en_16k.onnx"));
    QVERIFY2(QFileInfo::exists(model), "hiányzik a modell a sandboxból");
    QVERIFY2(!QDir(home).absolutePath().startsWith(QDir::homePath() + QStringLiteral("/.tanara")),
             "a sandbox nem mutathat a valódi ~/.tanara-ra");

    MeetingStore store(rec, home);
    store.rebuildIndexFromDisk();
    PeopleStore people(QDir(home).filePath(QStringLiteral("people.json")));
    VoiceprintStore prints(QDir(home).filePath(QStringLiteral("voiceprints.json")));

    int n = 0;
    for (const Meeting& entry : store.loadAll()) {
        const Meeting m = store.load(entry.id);
        const QVector<TranscriptLine> lines = loadTranscriptLines(m.folder);
        if (lines.isEmpty()) continue;
        ++n;
        const bool fresh = qEnvironmentVariableIsSet("TANARA_SANDBOX_FRESH");
        if (fresh) UtteranceEmbeddingCache::remove(m.folder);

        SpeakerEditor ed(&store, &people, &prints, m.id);
        ed.setEmbedderFactory(voiceUtteranceEmbedderFactory(model));
        QSignalSpy done(&ed, &SpeakerEditor::embeddingFinished);
        QElapsedTimer timer;
        timer.start();
        ed.startEmbedding();
        if (done.isEmpty()) QVERIFY(done.wait(15 * 60 * 1000));
        const double secs = timer.elapsed() / 1000.0;
        QVERIFY2(done.last().at(0).toBool(), qPrintable(ed.embeddingError()));

        int embeddable = 0;
        for (const TranscriptLine& l : lines)
            if (l.endMs - l.startMs >= kMinEmbedMs) ++embeddable;
        const int unc = ed.uncertainCount();
        qInfo().noquote() << QStringLiteral(
            "MEETING #%1: %2 perc, %3 sor (%4 embeddelhető), %5 beszélő | embedding %6 mp | "
            "bizonytalan %7 sor = %8% (az embeddelhetők %9%-a)")
            .arg(n).arg(m.durationMs / 60000.0, 0, 'f', 1).arg(lines.size()).arg(embeddable)
            .arg(ed.speakers().size()).arg(secs, 0, 'f', 1).arg(unc)
            .arg(100.0 * unc / lines.size(), 0, 'f', 1)
            .arg(embeddable ? 100.0 * unc / embeddable : 0.0, 0, 'f', 1);

        // ---- egy valódi művelet ideje (overlay + meeting.json + transcript.md írás), majd undo ----
        {
            QString firstLong;
            for (const EditorUtterance& u : ed.utterances())
                if (u.endMs - u.startMs >= kReliableMs) { firstLong = u.id; break; }
            if (!firstLong.isEmpty()) {
                QElapsedTimer op;
                op.start();
                const QString key = ed.moveUtterancesToNewParticipant({firstLong});
                const qint64 moveMs = op.restart();
                const int suggested = ed.suggestion().utteranceIds.size();
                ed.undo();
                const qint64 undoMs = op.elapsed();
                QVERIFY(!key.isEmpty());
                QVERIFY(!ed.canUndo());
                QVERIFY(!loadOverlay(m.folder).hasEdits());    // a sandbox visszaállt
                qInfo().noquote() << QStringLiteral(
                    "   művelet: 1 sor áthelyezése %1 ms (javaslat: %2 sor), visszavonás %3 ms")
                    .arg(moveMs).arg(suggested).arg(undoMs);
            }
        }

        // ---- illeszkedés-eloszlások a nyers diarizáción ----
        const UtteranceEmbeddingCache cache =
            UtteranceEmbeddingCache::load(m.folder, transcriptFingerprint(lines));
        const QHash<QString, QVector<float>> vectors = cache.vectors(QStringLiteral("campplus"));
        QStringList raws;
        for (const TranscriptLine& l : lines)
            if (!raws.contains(l.rawLabel)) raws << l.rawLabel;
        QVector<AnalysisLine> al(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            al[i].speaker = raws.indexOf(lines[i].rawLabel);
            al[i].durationMs = lines[i].endMs - lines[i].startMs;
            const auto it = vectors.constFind(lines[i].id);
            if (it != vectors.constEnd() && !it->isEmpty()) al[i].embedding = &it.value();
        }
        const QVector<LineFit> fits = computeFits(al, raws.size());
        QVector<double> ownLong, ownShort, gapLong, gapShort;
        for (int i = 0; i < fits.size(); ++i) {
            if (std::isnan(fits[i].own)) continue;
            const bool rel = al[i].durationMs >= kReliableMs;
            (rel ? ownLong : ownShort).append(fits[i].own);
            if (!std::isnan(fits[i].other))
                (rel ? gapLong : gapShort).append(fits[i].own - fits[i].other);
        }
        qInfo().noquote() << "   saját-illeszkedés  >=3s:" << dist(ownLong) << "| 1.5-3s:" << dist(ownShort);
        qInfo().noquote() << "   saját−másik       >=3s:" << dist(gapLong) << "| 1.5-3s:" << dist(gapShort);

        // ---- diagnosztika (TANARA_SANDBOX_DIAG): a megbízható sorok k-közép klaszterei a
        //      nyers címkékkel szemben — megmutatja, ha egy címke valójában több hang. ----
        if (qEnvironmentVariableIsSet("TANARA_SANDBOX_DIAG")) {
            QVector<int> rel;
            for (int i = 0; i < al.size(); ++i)
                if (al[i].hasEmbedding() && al[i].durationMs >= kReliableMs) rel.append(i);
            auto cosv = [](const QVector<float>& a, const QVector<float>& b) {
                double s = 0; for (int k = 0; k < a.size(); ++k) s += double(a[k]) * b[k]; return s;
            };
            for (int k = 2; k <= 4 && rel.size() > 8; ++k) {
                QVector<QVector<float>> cent{*al[rel.first()].embedding};
                while (cent.size() < k) {   // legtávolabbi pont
                    int best = -1; double bestD = 2;
                    for (int i : rel) {
                        double mx = -2;
                        for (const auto& c : cent) mx = std::max(mx, cosv(*al[i].embedding, c));
                        if (mx < bestD) { bestD = mx; best = i; }
                    }
                    cent.append(*al[best].embedding);
                }
                QVector<int> asg(al.size(), -1);
                for (int iter = 0; iter < 20; ++iter) {
                    for (int i : rel) {
                        int b = 0; double bd = -2;
                        for (int c = 0; c < k; ++c) {
                            const double d = cosv(*al[i].embedding, cent[c]);
                            if (d > bd) { bd = d; b = c; }
                        }
                        asg[i] = b;
                    }
                    for (int c = 0; c < k; ++c) {
                        QVector<float> s(cent[c].size(), 0.0f);
                        for (int i : rel) if (asg[i] == c)
                            for (int j = 0; j < s.size(); ++j) s[j] += (*al[i].embedding)[j];
                        cent[c] = VoiceprintStore::l2normalize(s);
                    }
                }
                QString table;
                double within = 0;
                for (int i : rel) within += cosv(*al[i].embedding, cent[asg[i]]);
                for (int r = 0; r < raws.size(); ++r) {
                    table += QStringLiteral(" címke%1:[").arg(r + 1);
                    for (int c = 0; c < k; ++c) {
                        int cnt = 0;
                        for (int i : rel) if (al[i].speaker == r && asg[i] == c) ++cnt;
                        table += QString::number(cnt) + (c + 1 < k ? QStringLiteral(",") : QStringLiteral("]"));
                    }
                }
                QString cc;
                for (int a = 0; a < k; ++a) for (int b = a + 1; b < k; ++b)
                    cc += QStringLiteral(" %1").arg(cosv(cent[a], cent[b]), 0, 'f', 2);
                qInfo().noquote() << QStringLiteral("   k=%1 átlag-illeszkedés %2 |%3 | centroid-cos:%4")
                    .arg(k).arg(within / rel.size(), 0, 'f', 2).arg(table, cc);
            }
        }

        // ---- javaslat-szimuláció: Y beszélőt X-be mossuk, majd k sorát „kézzel" kiemeljük ----
        for (int x = 0; x < raws.size(); ++x) for (int y = 0; y < raws.size(); ++y) {
            if (x == y) continue;
            QVector<int> yLines;
            int xCount = 0;
            for (int i = 0; i < al.size(); ++i) {
                if (!al[i].hasEmbedding()) continue;
                if (al[i].speaker == y) yLines.append(i);
                if (al[i].speaker == x) ++xCount;
            }
            if (yLines.size() < 10 || xCount < 10) continue;
            for (int k : {1, 3}) {
                QVector<AnalysisLine> sim = al;
                const int target = raws.size();     // az új (kézzel létrehozott) beszélő
                int seeded = 0;
                for (int i : yLines) {
                    sim[i].speaker = x;
                    if (seeded < k && sim[i].durationMs >= kReliableMs) {
                        sim[i].speaker = target;
                        sim[i].locked = true;
                        ++seeded;
                    }
                }
                const QVector<int> hits = suggestSimilar(sim, x, target);
                int tp = 0, truth = 0;
                for (int i : yLines) if (sim[i].speaker == x) ++truth;
                for (int i : hits) if (al[i].speaker == y) ++tp;
                qInfo().noquote() << QStringLiteral(
                    "   javaslat: beszélő %1 → %2 közé mosva, %3 mag-sor: ajánlott %4, ebből helyes %5 "
                    "(precision %6%, recall %7% a %8 sorból)")
                    .arg(y + 1).arg(x + 1).arg(k).arg(hits.size()).arg(tp)
                    .arg(hits.isEmpty() ? 0.0 : 100.0 * tp / hits.size(), 0, 'f', 0)
                    .arg(truth ? 100.0 * tp / truth : 0.0, 0, 'f', 0).arg(truth);
            }
        }
    }
    QVERIFY2(n > 0, "a sandboxban nincs átírt meeting");
}

QTEST_GUILESS_MAIN(SpeakerSandboxTest)
#include "test_speaker_sandbox.moc"
