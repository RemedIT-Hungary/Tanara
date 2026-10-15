#include "tanara/processing/diarization/DiarizationEval.h"

#include "tanara/processing/diarization/SegmentationMath.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tanara::processing::diareval {

double DerResult::der() const
{
    if (scored > 0) return errorSeconds() / scored;
    return falseAlarm > 0 ? 1.0 : 0.0;
}

DerResult& DerResult::operator+=(const DerResult& o)
{
    scored += o.scored;
    missed += o.missed;
    falseAlarm += o.falseAlarm;
    confusion += o.confusion;
    refSpeakers += o.refSpeakers;
    hypSpeakers += o.hypSpeakers;
    mapping.clear();
    return *this;
}

QStringList speakersOf(const QVector<EvalTurn>& turns)
{
    QStringList out;
    for (const EvalTurn& t : turns)
        if (t.end > t.start && !out.contains(t.speaker)) out << t.speaker;
    return out;
}

namespace {

// Frame index range [a, b) of a time range.
std::pair<qsizetype, qsizetype> frameRange(double start, double end, double step, qsizetype n)
{
    const qsizetype a = std::clamp<qsizetype>(qsizetype(std::llround(start / step)), 0, n);
    const qsizetype b = std::clamp<qsizetype>(qsizetype(std::llround(end / step)), 0, n);
    return {a, std::max(a, b)};
}

// Per speaker: a 0/1 frame row.
QVector<QVector<char>> activityRows(const QVector<EvalTurn>& turns, const QStringList& speakers, double step,
                                    qsizetype n)
{
    QVector<QVector<char>> rows(speakers.size(), QVector<char>(n, 0));
    for (const EvalTurn& t : turns) {
        const qsizetype s = speakers.indexOf(t.speaker);
        if (s < 0) continue;
        const auto [a, b] = frameRange(t.start, t.end, step, n);
        std::fill(rows[s].begin() + a, rows[s].begin() + b, char(1));
    }
    return rows;
}

} // namespace

DerResult computeDer(const QVector<EvalTurn>& ref, const QVector<EvalTurn>& hyp, const DerOptions& opts,
                     const QVector<Interval>& excluded)
{
    DerResult r;
    const double step = opts.step > 0 ? opts.step : 0.01;
    const QStringList refSpk = speakersOf(ref), hypSpk = speakersOf(hyp);
    r.refSpeakers = int(refSpk.size());
    r.hypSpeakers = int(hypSpk.size());
    double end = 0;
    for (const EvalTurn& t : ref) end = std::max(end, t.end);
    for (const EvalTurn& t : hyp) end = std::max(end, t.end);
    const qsizetype n = qsizetype(std::ceil(end / step)) + 1;
    if (end <= 0) return r;

    // The scored frames: everything but the collars and the excluded regions.
    QVector<char> scored(n, 1);
    auto unscore = [&](double a, double b) {
        const auto [fa, fb] = frameRange(a, b, step, n);
        std::fill(scored.begin() + fa, scored.begin() + fb, char(0));
    };
    if (opts.collar > 0)
        for (const EvalTurn& t : ref) {
            if (t.end <= t.start) continue;
            unscore(t.start - opts.collar, t.start + opts.collar);
            unscore(t.end - opts.collar, t.end + opts.collar);
        }
    for (const Interval& x : excluded) unscore(x.start, x.end);

    const QVector<QVector<char>> R = activityRows(ref, refSpk, step, n);
    const QVector<QVector<char>> H = activityRows(hyp, hypSpk, step, n);

    // The optimal mapping: the largest total overlap on the scored frames.
    const int nh = int(hypSpk.size()), nr = int(refSpk.size());
    QVector<int> map(nh, -1);
    if (nh > 0 && nr > 0) {
        QVector<double> cost(qsizetype(nh) * nr, 0.0);
        for (int h = 0; h < nh; ++h)
            for (int s = 0; s < nr; ++s) {
                qint64 ov = 0;
                for (qsizetype f = 0; f < n; ++f) ov += scored[f] && H[h][f] && R[s][f];
                cost[qsizetype(h) * nr + s] = -double(ov);
            }
        const QVector<int> assign = segmentation::hungarianAssign(cost, nh, nr);
        for (int h = 0; h < nh; ++h) {
            const int s = assign.value(h, -1);
            if (s >= 0 && cost[qsizetype(h) * nr + s] < 0) {
                map[h] = s;
                r.mapping.insert(hypSpk[h], refSpk[s]);
            }
        }
    }

    qint64 sumRef = 0, miss = 0, fa = 0, conf = 0;
    for (qsizetype f = 0; f < n; ++f) {
        if (!scored[f]) continue;
        int nRef = 0, nHyp = 0, correct = 0;
        for (int s = 0; s < nr; ++s) nRef += R[s][f];
        for (int h = 0; h < nh; ++h) {
            if (!H[h][f]) continue;
            ++nHyp;
            if (map[h] >= 0 && R[map[h]][f]) ++correct;
        }
        sumRef += nRef;
        miss += std::max(0, nRef - nHyp);
        fa += std::max(0, nHyp - nRef);
        conf += std::min(nRef, nHyp) - correct;
    }
    r.scored = double(sumRef) * step;
    r.missed = double(miss) * step;
    r.falseAlarm = double(fa) * step;
    r.confusion = double(conf) * step;
    return r;
}

// ---- reference files ----

const TrackReference* Reference::track(const QString& trackId) const
{
    for (const TrackReference& t : tracks)
        if (t.trackId == trackId) return &t;
    return nullptr;
}

Reference parseReferenceJson(const QByteArray& json, QString* error)
{
    Reference ref;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("not a JSON object: %1").arg(pe.errorString());
        return {};
    }
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("tanara-diarization-ref")) {
        if (error) *error = QStringLiteral("unknown reference format (expected \"tanara-diarization-ref\")");
        return {};
    }
    ref.source = o.value(QStringLiteral("source")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("notes")).toArray()) ref.notes << v.toString();
    for (const QJsonValue& tv : o.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject to = tv.toObject();
        TrackReference tr;
        tr.trackId = to.value(QStringLiteral("trackId")).toString();
        tr.file = to.value(QStringLiteral("file")).toString();
        tr.startOffsetMs = qint64(to.value(QStringLiteral("startOffsetMs")).toDouble());
        for (const QJsonValue& v : to.value(QStringLiteral("turns")).toArray()) {
            const QJsonObject t = v.toObject();
            tr.turns.append({t.value(QStringLiteral("start")).toDouble(), t.value(QStringLiteral("end")).toDouble(),
                             t.value(QStringLiteral("speaker")).toString()});
        }
        for (const QJsonValue& v : to.value(QStringLiteral("excluded")).toArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() == 2) tr.excluded.append({a[0].toDouble(), a[1].toDouble()});
        }
        ref.tracks.append(tr);
    }
    return ref;
}

QJsonObject referenceToJson(const Reference& ref)
{
    QJsonObject o;
    o[QStringLiteral("format")] = QStringLiteral("tanara-diarization-ref");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("source")] = ref.source;
    o[QStringLiteral("notes")] = QJsonArray::fromStringList(ref.notes);
    QJsonArray tracks;
    for (const TrackReference& tr : ref.tracks) {
        QJsonObject to;
        to[QStringLiteral("trackId")] = tr.trackId;
        to[QStringLiteral("file")] = tr.file;
        to[QStringLiteral("startOffsetMs")] = double(tr.startOffsetMs);
        QJsonArray turns, excl;
        for (const EvalTurn& t : tr.turns)
            turns.append(QJsonObject{{QStringLiteral("start"), t.start}, {QStringLiteral("end"), t.end},
                                     {QStringLiteral("speaker"), t.speaker}});
        for (const Interval& x : tr.excluded) excl.append(QJsonArray{x.start, x.end});
        to[QStringLiteral("turns")] = turns;
        to[QStringLiteral("excluded")] = excl;
        tracks.append(to);
    }
    o[QStringLiteral("tracks")] = tracks;
    return o;
}

Reference parseRttm(const QString& text, QString* error)
{
    Reference ref;
    ref.source = QStringLiteral("rttm");
    QMap<QString, int> index;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")));
        if (f.first() != QLatin1String("SPEAKER")) continue;
        bool ok1 = false, ok2 = false;
        const double start = f.value(3).toDouble(&ok1), dur = f.value(4).toDouble(&ok2);
        if (f.size() < 8 || !ok1 || !ok2) {
            if (error) *error = QStringLiteral("bad RTTM line %1").arg(i + 1);
            return {};
        }
        const QString file = f[1];
        if (!index.contains(file)) {
            index.insert(file, int(ref.tracks.size()));
            TrackReference tr;
            tr.trackId = file;
            tr.file = file;
            ref.tracks.append(tr);
        }
        ref.tracks[index.value(file)].turns.append({start, start + dur, f[7]});
    }
    return ref;
}

QString toRttm(const QString& fileId, const QVector<EvalTurn>& turns)
{
    QString out;
    for (const EvalTurn& t : turns)
        out += QStringLiteral("SPEAKER %1 1 %2 %3 <NA> <NA> %4 <NA> <NA>\n")
                   .arg(fileId, QString::number(t.start, 'f', 3), QString::number(t.end - t.start, 'f', 3),
                        QString(t.speaker).replace(QLatin1Char(' '), QLatin1Char('_')));
    return out;
}

// ---- a reference from provider tokens ----

QVector<Interval> utterancesOf(QVector<RefToken> tokens, const RefBuildOptions& opts)
{
    std::sort(tokens.begin(), tokens.end(), [](const RefToken& a, const RefToken& b) { return a.startMs < b.startMs; });
    QVector<Interval> raw;
    for (const RefToken& t : tokens) {
        const double s = t.startMs / 1000.0, e = std::max(t.startMs, t.endMs) / 1000.0;
        if (!raw.isEmpty() && s - raw.last().end <= opts.maxGapSec) raw.last().end = std::max(raw.last().end, e);
        else raw.append({s, e});
    }
    QVector<Interval> out;
    for (Interval u : raw) {
        u.start = std::max(0.0, u.start - opts.leadSec);
        u.end += opts.tailSec;
        if (!out.isEmpty() && u.start <= out.last().end) out.last().end = std::max(out.last().end, u.end);
        else out.append(u);
    }
    return out;
}

QVector<EvalTurn> turnsFromTokens(const QVector<RefToken>& tokens, const RefBuildOptions& opts)
{
    QMap<QString, QVector<RefToken>> bySpeaker;
    for (const RefToken& t : tokens) bySpeaker[t.speaker].append(t);
    QVector<EvalTurn> out;
    for (auto it = bySpeaker.constBegin(); it != bySpeaker.constEnd(); ++it)
        for (const Interval& u : utterancesOf(it.value(), opts)) out.append({u.start, u.end, it.key()});
    std::sort(out.begin(), out.end(), [](const EvalTurn& a, const EvalTurn& b) {
        return a.start != b.start ? a.start < b.start : a.speaker < b.speaker;
    });
    return out;
}

QString assignTrack(const QHash<QString, double>& votes, double majority)
{
    double total = 0, best = 0;
    QString winner;
    // Deterministic: ties go to the alphabetically first track.
    QStringList keys = votes.keys();
    keys.sort();
    for (const QString& k : keys) {
        total += votes.value(k);
        if (votes.value(k) > best) {
            best = votes.value(k);
            winner = k;
        }
    }
    return total > 0 && best >= majority * total ? winner : QString();
}

} // namespace tanara::processing::diareval
