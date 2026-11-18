// RemoteLineClusters: the remote lines' owners from the meeting's own voices, on synthetic vectors (no audio,
// no models, no I/O): several voices under one raw speaker are separated, one voice stays one cluster, a short
// line follows its turn, a locked line is not changed and pulls its cluster's owner, an unmatched voice becomes
// anonymous, nothing Remote goes to the local participant, a rerun gives the same result, and the single-track
// mode.
#include <QtTest>

#include <QRandomGenerator>

#include <cmath>

#include "tanara/editing/review/RemoteLineClusters.h"

using namespace tanara;
using namespace tanara::editing;
namespace rl = tanara::editing::remotelines;

namespace {

constexpr int kDim = 16;

// A voice's line vector: the voice's axis + a little deterministic noise, L2-normalized.
QVector<float> voice(int axis, quint32 seed, double noise = 0.25)
{
    QRandomGenerator rng(seed * 7919u + quint32(axis) * 104729u + 17u);
    QVector<float> v(kDim, 0.0f);
    v[axis] = 1.0f;
    for (int k = 0; k < kDim; ++k) v[k] += float(noise * (rng.generateDouble() - 0.5));
    double n = 0.0;
    for (float x : v) n += double(x) * x;
    n = std::sqrt(n);
    for (float& x : v) x = float(x / n);
    return v;
}

struct Builder {
    QVector<rl::Line> lines;
    qint64 t = 0;
    // A line of `axis`'s voice (axis < 0: no vector), after a gap.
    rl::Line& add(int axis, qint64 durMs, const QString& raw, Side side = Side::Remote, qint64 gapMs = 500)
    {
        rl::Line l;
        l.id = QStringLiteral("u%1").arg(t + gapMs);
        l.startMs = t + gapMs;
        l.endMs = l.startMs + durMs;
        l.rawLabel = raw;
        l.side = side;
        if (axis >= 0) l.embedding = voice(axis, quint32(lines.size()) + 1);
        l.currentTarget = raw;
        t = l.endMs;
        lines.append(l);
        return lines.last();
    }
    // The windows covering the lines of one voice (the pre-transcript analysis found that voice there).
    QVector<QPair<qint64, qint64>> windowsOf(const QVector<int>& idx) const
    {
        QVector<QPair<qint64, qint64>> w;
        for (int i : idx) w.append({lines[i].startMs, lines[i].endMs});
        return w;
    }
};

rl::Target target(const QString& id, bool named = true, bool local = false)
{
    rl::Target t;
    t.id = id;
    t.named = named;
    t.local = local;
    return t;
}

// The options of a small synthetic meeting (the cluster minimum scaled down).
rl::Options smallOptions()
{
    rl::Options o;
    o.minClusterMs = 8000;
    return o;
}

} // namespace

class RemoteLineClustersTest : public QObject {
    Q_OBJECT
private slots:
    void separatesTwoVoicesUnderOneRawSpeaker();
    void separatesThreeVoicesUnmatchedIsAnonymous();
    void oneVoiceStaysOneCluster();
    void shortLineFollowsItsTurn();
    void lockedLineIsKeptAndPullsItsCluster();
    void bindsByVoiceprint();
    void nothingRemoteGoesToTheLocalParticipant();
    void rerunIsIdempotent();
    void singleTrackMode();
};

void RemoteLineClustersTest::separatesTwoVoicesUnderOneRawSpeaker()
{
    Builder b;
    QVector<int> a, c;
    for (int k = 0; k < 8; ++k) {
        b.add(0, 4000, "speaker:1"); a << b.lines.size() - 1;
        b.add(1, 4000, "speaker:1"); c << b.lines.size() - 1;
    }
    rl::Target ta = target("A"), tb = target("B");
    ta.windows = b.windowsOf(a);
    tb.windows = b.windowsOf(c);
    const rl::Result r = rl::assign(b.lines, {ta, tb}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.clusters.size(), 2);
    for (int i : a) QCOMPARE(r.lines[i].target, QStringLiteral("A"));
    for (int i : c) QCOMPARE(r.lines[i].target, QStringLiteral("B"));
    for (const rl::Cluster& cl : r.clusters) QCOMPARE(cl.bindBasis, QStringLiteral("windows"));
}

void RemoteLineClustersTest::separatesThreeVoicesUnmatchedIsAnonymous()
{
    Builder b;
    QVector<int> a, c, x;
    for (int k = 0; k < 6; ++k) {
        b.add(0, 4000, "speaker:1"); a << b.lines.size() - 1;
        b.add(1, 4000, "speaker:1"); c << b.lines.size() - 1;
        b.add(2, 4000, "speaker:2"); x << b.lines.size() - 1;
    }
    rl::Target ta = target("A"), tb = target("B"), anon = target("speaker:2", /*named*/ false);
    ta.windows = b.windowsOf(a);
    tb.windows = b.windowsOf(c);
    const rl::Result r = rl::assign(b.lines, {ta, tb, anon}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.clusters.size(), 3);
    QCOMPARE(r.anonymous, 1);
    for (int i : a) QCOMPARE(r.lines[i].target, QStringLiteral("A"));
    for (int i : c) QCOMPARE(r.lines[i].target, QStringLiteral("B"));
    for (int i : x) {
        QCOMPARE(r.lines[i].target, QStringLiteral("anon:1"));
        QVERIFY(rl::isAnonymous(r.lines[i].target));
    }
}

void RemoteLineClustersTest::oneVoiceStaysOneCluster()
{
    Builder b;
    for (int k = 0; k < 12; ++k) b.add(3, 3000 + 400 * (k % 4), "speaker:4");
    rl::Target t = target("D");
    t.windows = b.windowsOf({0, 1, 2, 3});
    const rl::Result r = rl::assign(b.lines, {t}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.clusters.size(), 1);
    for (const rl::LineDecision& d : r.lines) {
        QCOMPARE(d.target, QStringLiteral("D"));
        QVERIFY(!d.weak);
    }
}

void RemoteLineClustersTest::shortLineFollowsItsTurn()
{
    Builder b;
    QVector<int> a, c;
    for (int k = 0; k < 5; ++k) {
        b.add(0, 4000, "speaker:1"); a << b.lines.size() - 1;
        b.add(1, 4000, "speaker:1"); c << b.lines.size() - 1;
    }
    // A's turn: long, short (no vector), long; the short one carries B's raw label (the provider's mistake).
    b.add(0, 4000, "speaker:1", Side::Remote, 3000); a << b.lines.size() - 1;
    const int shortIdx = int(b.lines.size());
    b.add(-1, 700, "speaker:2", Side::Remote, 300);
    b.add(0, 4000, "speaker:1", Side::Remote, 300); a << b.lines.size() - 1;
    rl::Target ta = target("A"), tb = target("B");
    ta.windows = b.windowsOf(a);
    tb.windows = b.windowsOf(c);
    const rl::Result r = rl::assign(b.lines, {ta, tb}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.lines[shortIdx].target, QStringLiteral("A"));
    QCOMPARE(r.lines[shortIdx].basis, rl::Basis::Turn);
    QVERIFY(!r.lines[shortIdx].weak);   // both neighbours agree
}

void RemoteLineClustersTest::lockedLineIsKeptAndPullsItsCluster()
{
    Builder b;
    QVector<int> a, c;
    for (int k = 0; k < 6; ++k) {
        b.add(0, 4000, "speaker:1"); a << b.lines.size() - 1;
        b.add(1, 4000, "speaker:1"); c << b.lines.size() - 1;
    }
    // Two of C's lines confirmed for "C" (a speaker with no windows, no voiceprint): without them C's voice
    // would be anonymous; with them the whole voice is C's, and the confirmed lines themselves stay as they are.
    b.lines[c[0]].lockedTarget = QStringLiteral("C");
    b.lines[c[1]].lockedTarget = QStringLiteral("C");
    rl::Target ta = target("A");
    ta.windows = b.windowsOf(a);
    const rl::Result r = rl::assign(b.lines, {ta, target("C", /*named*/ false)}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.anonymous, 0);
    QCOMPARE(r.lines[c[0]].basis, rl::Basis::Unchanged);
    QVERIFY(r.lines[c[0]].target.isEmpty());
    QVERIFY(r.lines[c[1]].target.isEmpty());
    for (int k = 2; k < c.size(); ++k) QCOMPARE(r.lines[c[k]].target, QStringLiteral("C"));
    for (int i : a) QCOMPARE(r.lines[i].target, QStringLiteral("A"));
    bool lockedCluster = false;
    for (const rl::Cluster& cl : r.clusters)
        if (cl.target == QLatin1String("C")) { lockedCluster = cl.bindBasis == QLatin1String("locked"); QCOMPARE(cl.lockedLines, 2); }
    QVERIFY(lockedCluster);
}

void RemoteLineClustersTest::bindsByVoiceprint()
{
    Builder b;
    for (int k = 0; k < 6; ++k) { b.add(0, 4000, "speaker:1"); b.add(1, 4000, "speaker:1"); }
    rl::Target ta = target("A"), tb = target("B");
    ta.prints = {voice(0, 999, 0.1)};
    tb.prints = {voice(1, 998, 0.1)};
    const rl::Result r = rl::assign(b.lines, {ta, tb}, {}, smallOptions());
    QVERIFY(r.ran);
    QCOMPARE(r.lines[0].target, QStringLiteral("A"));
    QCOMPARE(r.lines[1].target, QStringLiteral("B"));
    for (const rl::Cluster& cl : r.clusters) QCOMPARE(cl.bindBasis, QStringLiteral("voiceprint"));
}

void RemoteLineClustersTest::nothingRemoteGoesToTheLocalParticipant()
{
    Builder b;
    QVector<int> remote, local;
    for (int k = 0; k < 6; ++k) {
        b.add(0, 4000, "speaker:1"); remote << b.lines.size() - 1;
        rl::Line& l = b.add(5, 4000, "speaker:1", Side::Local);
        l.currentTarget = QStringLiteral("ME");
        local << b.lines.size() - 1;
    }
    // A Remote line currently on the local participant, without a vector and with no remote neighbour.
    rl::Line& stray = b.add(-1, 900, "speaker:9", Side::Remote, 60000);
    stray.currentTarget = QStringLiteral("ME");
    const int strayIdx = int(b.lines.size()) - 1;
    // The local participant "sounds like" the remote voice and has windows over it: still never chosen.
    rl::Target me = target("ME", true, /*local*/ true);
    me.prints = {voice(0, 997, 0.05)};
    me.windows = b.windowsOf(remote);
    const rl::Result r = rl::assign(b.lines, {me, target("speaker:1", /*named*/ false)}, {}, smallOptions());
    QVERIFY(r.ran);
    for (int i : remote) {
        QVERIFY(!r.lines[i].target.isEmpty());
        QVERIFY(r.lines[i].target != QLatin1String("ME"));
    }
    for (int i : local) QVERIFY(r.lines[i].target.isEmpty());   // the local side is not touched
    QVERIFY(r.lines[strayIdx].target != QLatin1String("ME"));
    QCOMPARE(r.lines[strayIdx].basis, rl::Basis::Raw);
}

void RemoteLineClustersTest::rerunIsIdempotent()
{
    Builder b;
    for (int k = 0; k < 7; ++k) { b.add(0, 4000, "speaker:1"); b.add(1, 3000, "speaker:2"); b.add(-1, 600, "speaker:1"); }
    rl::Target ta = target("A");
    ta.windows = b.windowsOf({0, 3, 6});
    const rl::Result r1 = rl::assign(b.lines, {ta}, {}, smallOptions());
    // The second run sees the first one's result as the current owners (the automatic assignment is not locked).
    QVector<rl::Line> again = b.lines;
    for (int i = 0; i < again.size(); ++i)
        if (!r1.lines[i].target.isEmpty()) again[i].currentTarget = r1.lines[i].target;
    const rl::Result r2 = rl::assign(again, {ta}, {}, smallOptions());
    QCOMPARE(r2.lines.size(), r1.lines.size());
    for (int i = 0; i < r1.lines.size(); ++i) {
        QCOMPARE(r2.lines[i].target, r1.lines[i].target);
        QCOMPARE(r2.lines[i].weak, r1.lines[i].weak);
    }
}

void RemoteLineClustersTest::singleTrackMode()
{
    // No side knowledge: every line Unknown. With sideKnown the Unknown lines do not seed, so nothing is
    // clustered; in the single-track mode they are, and any target (also a local one) may own a voice.
    Builder b;
    QVector<int> a, c;
    for (int k = 0; k < 6; ++k) {
        b.add(0, 4000, "speaker:1", Side::Unknown); a << b.lines.size() - 1;
        b.add(1, 4000, "speaker:1", Side::Unknown); c << b.lines.size() - 1;
    }
    rl::Target me = target("ME", true, true), tb = target("B");
    me.windows = b.windowsOf(a);
    tb.windows = b.windowsOf(c);
    const rl::Result two = rl::assign(b.lines, {me, tb}, {}, smallOptions());
    QVERIFY(!two.ran);
    QCOMPARE(two.skipped, QStringLiteral("no-seeds"));
    rl::Options single = smallOptions();
    single.sideKnown = false;
    const rl::Result one = rl::assign(b.lines, {me, tb}, {}, single);
    QVERIFY(one.ran);
    for (int i : a) QCOMPARE(one.lines[i].target, QStringLiteral("ME"));
    for (int i : c) QCOMPARE(one.lines[i].target, QStringLiteral("B"));
}

QTEST_GUILESS_MAIN(RemoteLineClustersTest)
#include "test_remote_line_clusters.moc"
