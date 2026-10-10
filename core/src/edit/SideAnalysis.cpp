#include "tanara/edit/SideAnalysis.h"

#include "tanara/Logging.h"
#include "tanara/edit/SpeakerAnalysis.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tanara {

QString sideName(Side s)
{
    switch (s) {
    case Side::Local:   return QStringLiteral("local");
    case Side::Remote:  return QStringLiteral("remote");
    case Side::Mixed:   return QStringLiteral("mixed");
    case Side::Unknown: break;
    }
    return QStringLiteral("unknown");
}

Side sideFromName(const QString& name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("local")) return Side::Local;
    if (n == QLatin1String("remote")) return Side::Remote;
    if (n == QLatin1String("mixed")) return Side::Mixed;
    return Side::Unknown;
}

void SideCounts::add(Side s)
{
    switch (s) {
    case Side::Local:   ++local; break;
    case Side::Remote:  ++remote; break;
    case Side::Mixed:   ++mixed; break;
    case Side::Unknown: ++unknown; break;
    }
}

namespace sides {

namespace {

// A zajpadló feletti dB → a padló FELETTI lineáris teljesítmény (a padló maga 0).
double excessPower(float db) { return std::max(0.0, std::pow(10.0, double(db) / 10.0) - 1.0); }

} // namespace

float localShareFromDb(float micDb, float loopDb)
{
    const double pm = excessPower(micDb), pl = excessPower(loopDb);
    if (pm + pl <= 0.0) return qQNaN();
    return float(pm / (pm + pl));
}

Thresholds adaptiveThresholds(const QVector<float>& shares)
{
    Thresholds th;
    QVector<double> v;
    for (float s : shares)
        if (!std::isnan(s)) v.append(s);
    const int n = int(v.size());
    if (n < kAdaptiveMinLines) return th;
    std::sort(v.begin(), v.end());

    // Egzakt 1D 2-közép: a rendezett sor kettévágása [0,k) | [k,n) a legkisebb négyzetes hibával.
    QVector<double> pre(n + 1, 0.0), pre2(n + 1, 0.0);
    for (int i = 0; i < n; ++i) {
        pre[i + 1] = pre[i] + v[i];
        pre2[i + 1] = pre2[i] + v[i] * v[i];
    }
    auto sse = [&](int a, int b) {   // [a,b)
        const double cnt = b - a, s = pre[b] - pre[a];
        return (pre2[b] - pre2[a]) - s * s / cnt;
    };
    int bestK = -1;
    double best = 0.0;
    for (int k = 1; k < n; ++k) {
        const double e = sse(0, k) + sse(k, n);
        if (bestK < 0 || e < best) { best = e; bestK = k; }
    }
    const int minCluster = std::max(kAdaptiveMinCluster, int(std::ceil(kAdaptiveMinClusterShare * n)));
    if (bestK < minCluster || n - bestK < minCluster) return th;
    const double cR = pre[bestK] / bestK;
    const double cL = (pre[n] - pre[bestK]) / (n - bestK);
    if (cL - cR < kAdaptiveMinGap || !(cR < 0.5 && cL > 0.5)) return th;

    const double mid = (cR + cL) / 2.0, half = (cL - cR) / 4.0;
    th.adaptive = true;
    th.centerRemote = float(cR);
    th.centerLocal = float(cL);
    th.loRemote = float(mid - half);
    th.hiLocal = float(mid + half);
    return th;
}

float speechLevelDb(const TrackActivity& t)
{
    QVector<float> active;
    for (float d : t.dbAboveFloor)
        if (!std::isnan(d) && d >= kActiveDb) active.append(d);
    if (active.isEmpty()) return qQNaN();
    const int k = std::clamp(int(std::floor(kSpeechPercentile * (active.size() - 1) + 0.5)), 0,
                             int(active.size()) - 1);
    std::nth_element(active.begin(), active.begin() + k, active.end());
    return active[k];
}

Side classifyLevels(float micRelDb, float loopRelDb)
{
    if (std::isnan(micRelDb) || std::isnan(loopRelDb)) return Side::Unknown;
    const bool micHigh = micRelDb >= kSpeechDb;
    const bool loopHigh = loopRelDb >= kSpeechDb;
    const bool micLow = micRelDb < kLowDb;
    if (micHigh && loopHigh) return loopRelDb <= kEchoDb ? Side::Local : Side::Mixed;
    if (micHigh) return Side::Local;
    if (loopHigh && micLow) return Side::Remote;
    return Side::Unknown;
}

Side classifyShare(float localShare, const Thresholds& th)
{
    if (std::isnan(localShare)) return Side::Unknown;
    if (localShare >= th.hiLocal) return Side::Local;
    if (localShare <= th.loRemote) return Side::Remote;
    return Side::Mixed;
}

} // namespace sides

namespace {

bool sameName(const QString& a, const QString& b)
{
    return !a.trimmed().isEmpty() && a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

Side sideOfKind(TrackKind k)
{
    return k == TrackKind::Mic ? Side::Local : k == TrackKind::Loopback ? Side::Remote : Side::Unknown;
}

bool opposite(Side a, Side b)
{
    return (a == Side::Local && b == Side::Remote) || (a == Side::Remote && b == Side::Local);
}

// Egy sávfajta együttes energiája az ablakban: a lefedő sávok padló feletti teljesítményének összege.
// false, ha egyik ilyen fajtájú sáv sem fedi le az ablakot.
bool kindEnergy(const MeetingActivity& act, TrackKind kind, qint64 s, qint64 e, float* db)
{
    bool any = false;
    double p = 0.0;
    for (const TrackActivity& t : act.tracks) {
        if (t.kind != kind) continue;
        const auto w = trackactivity::windowEnergyDb(t, s, e);
        if (!w) continue;
        any = true;
        p += std::max(0.0, std::pow(10.0, double(*w) / 10.0) - 1.0);
    }
    if (any) *db = float(10.0 * std::log10(1.0 + p));
    return any;
}

// Egy sávfajta szintje a beszédszinthez mérve: a lefedő sávok közül a legerősebb (ablak-energia −
// a sáv beszédszintje). -inf, ha egyiken sincs kActiveDb feletti jel; false, ha egyik sem fedi le.
bool kindRelativeDb(const MeetingActivity& act, const QMap<QString, float>& speech, TrackKind kind,
                    qint64 s, qint64 e, float* rel)
{
    bool any = false;
    float best = -std::numeric_limits<float>::infinity();
    for (const TrackActivity& t : act.tracks) {
        if (t.kind != kind) continue;
        const auto w = trackactivity::windowEnergyDb(t, s, e);
        if (!w) continue;
        any = true;
        const float level = speech.value(t.trackId, qQNaN());
        if (*w < sides::kActiveDb || std::isnan(level)) continue;
        best = std::max(best, *w - level);
    }
    if (any) *rel = best;
    return any;
}

bool hasKind(const MeetingActivity& act, TrackKind kind)
{
    return std::any_of(act.tracks.cbegin(), act.tracks.cend(),
                       [kind](const TrackActivity& t) { return t.kind == kind && t.coveredFrames() > 0; });
}

} // namespace

namespace sides {

Side sideOfTracks(const Meeting& m, const QStringList& trackIds)
{
    bool local = false, remote = false;
    for (const QString& id : trackIds)
        for (const Track& t : m.tracks) {
            if (t.id != id) continue;
            const Side sd = sideOfKind(t.kind);
            local |= sd == Side::Local;
            remote |= sd == Side::Remote;
        }
    if (local && remote) return Side::Mixed;
    return local ? Side::Local : remote ? Side::Remote : Side::Unknown;
}

QHash<QString, Side> learnedDefaults(const SideReport& r)
{
    QHash<QString, Side> out;
    for (const PersonSide& p : r.persons) {
        if (p.personName.isEmpty()) continue;
        if (p.side != Side::Local && p.side != Side::Remote) continue;
        if (p.basis != QLatin1String("lines") && p.basis != QLatin1String("override")
            && p.basis != QLatin1String("manual"))
            continue;
        out.insert(p.personName, p.side);
    }
    return out;
}

} // namespace sides

SideReport analyzeSides(const Meeting& m, const QVector<TranscriptLine>& lines,
                        const SpeakerOverlay& ov, const MeetingActivity& activity,
                        const QString& userName, const SideHints& hints)
{
    SideReport r;
    r.histogram = QVector<int>(10, 0);
    r.active = hasKind(activity, TrackKind::Mic) && hasKind(activity, TrackKind::Loopback);

    // Sorok: feloldott beszélő, zárolt / zajos jelzők (a szerkesztő szabályaival).
    QHash<QString, int> speakerIdx;
    QStringList speakerOrder;
    QVector<speakeredit::TimedLine> timed;
    r.lines.reserve(lines.size());
    for (const TranscriptLine& l : lines) {
        LineSide ls;
        ls.utteranceId = l.id;
        ls.startMs = l.startMs;
        ls.endMs = l.endMs;
        ls.rawLabel = l.rawLabel;
        ls.speakerKey = speakeredit::resolveSpeakerKey(ov, l);
        auto it = speakerIdx.constFind(ls.speakerKey);
        if (it == speakerIdx.constEnd()) {
            it = speakerIdx.insert(ls.speakerKey, int(speakerOrder.size()));
            speakerOrder << ls.speakerKey;
        }
        timed.append({l.startMs, l.endMs, *it});
        r.lines.append(ls);
    }
    const QVector<bool> autoNoisy = speakeredit::computeOverlapNoisy(timed);
    for (int i = 0; i < r.lines.size(); ++i) {
        LineSide& ls = r.lines[i];
        const auto o = ov.utterances.constFind(ls.utteranceId);
        const bool has = o != ov.utterances.constEnd();
        ls.locked = has && (o->corrected || o->confirmed);
        ls.noisy = (has && o->noisy.has_value()) ? *o->noisy : autoNoisy.value(i);
    }

    // Energia + localShare + beszédszinthez mért szint soronként (csak ha mindkét oldal van).
    for (const TrackActivity& t : activity.tracks) r.speechLevels.insert(t.trackId, sides::speechLevelDb(t));
    QVector<float> shares;
    if (r.active) {
        for (LineSide& ls : r.lines) {
            float mic = qQNaN(), loop = qQNaN();
            const bool hasMic = kindEnergy(activity, TrackKind::Mic, ls.startMs, ls.endMs, &mic);
            const bool hasLoop = kindEnergy(activity, TrackKind::Loopback, ls.startMs, ls.endMs, &loop);
            ls.micDb = hasMic ? mic : qQNaN();
            ls.loopDb = hasLoop ? loop : qQNaN();
            float micRel = qQNaN(), loopRel = qQNaN();
            if (kindRelativeDb(activity, r.speechLevels, TrackKind::Mic, ls.startMs, ls.endMs, &micRel))
                ls.micRelDb = micRel;
            if (kindRelativeDb(activity, r.speechLevels, TrackKind::Loopback, ls.startMs, ls.endMs, &loopRel))
                ls.loopRelDb = loopRel;
            if (!hasMic || !hasLoop) continue;                               // valamelyik oldal nem fedi le
            if (mic < sides::kActiveDb && loop < sides::kActiveDb) continue; // egyik oldalon sincs jel
            ls.localShare = sides::localShareFromDb(mic, loop);
            shares.append(ls.localShare);
        }
    }
    r.thresholds = sides::adaptiveThresholds(shares);
    for (LineSide& ls : r.lines) {
        ls.legacySide = sides::classifyShare(ls.localShare, r.thresholds);
        ls.side = r.active ? sides::classifyLevels(ls.micRelDb, ls.loopRelDb) : Side::Unknown;
        r.legacyTotals.add(ls.legacySide);
        r.totals.add(ls.side);
        r.rawLabels[ls.rawLabel].add(ls.side);
        if (!std::isnan(ls.localShare))
            ++r.histogram[std::clamp(int(ls.localShare * 10.0f), 0, 9)];
    }

    // Személyek oldala.
    QHash<QString, int> personIdx;
    for (const QString& key : speakerOrder) {
        PersonSide p;
        p.speakerKey = key;
        p.personName = speakeredit::speakerPerson(ov, m.speakerMap, key);
        p.displayName = speakeredit::speakerDisplayName(ov, m.speakerMap, key);
        personIdx.insert(key, int(r.persons.size()));
        r.persons.append(p);
    }
    for (const LineSide& ls : r.lines) {
        PersonSide& p = r.persons[personIdx.value(ls.speakerKey)];
        p.allLines.add(ls.side);
        if (!ls.locked || ls.noisy) continue;
        if (ls.side == Side::Local) ++p.localLines;
        else if (ls.side == Side::Remote) ++p.remoteLines;
        else if (ls.side == Side::Mixed) ++p.mixedLines;
    }
    const QHash<QString, QStringList>& manual = hints.speakerTracks.isEmpty() ? ov.speakerTracks
                                                                              : hints.speakerTracks;
    for (PersonSide& p : r.persons) {
        p.manualTracks = manual.value(p.speakerKey);
        p.manualSide = sides::sideOfTracks(m, p.manualTracks);
        if (!p.personName.isEmpty())
            p.learnedSide = hints.learnedSides.value(p.personName.toCaseFolded(), Side::Unknown);
        if (!r.active) { p.basis = QStringLiteral("none"); continue; }
        if (p.manualSide != Side::Unknown) {
            // A kézi sáv-beosztás az egyetlen igazság (a felhasználó mondta ki).
            p.side = p.manualSide;
            p.confidence = 1.0f;
            p.basis = QStringLiteral("manual");
            continue;
        }
        // Alapértelmezés: a tanult alapérték; különben a saját név → mic; a sávhoz kötött
        // (fixedSpeaker) beszélő → a sáv oldala.
        QString defBasis;
        if (p.learnedSide == Side::Local || p.learnedSide == Side::Remote) {
            p.defaultSide = p.learnedSide;
            defBasis = QStringLiteral("learned");
        } else if (sameName(userName, p.personName) || (p.personName.isEmpty() && sameName(userName, p.speakerKey))) {
            p.defaultSide = Side::Local;
            defBasis = QStringLiteral("user-name");
        } else {
            for (const Track& t : m.tracks) {
                if (!t.fixedSpeaker || sideOfKind(t.kind) == Side::Unknown) continue;
                if (sameName(t.speakerLabel, p.speakerKey) || sameName(t.speakerLabel, p.personName)) {
                    p.defaultSide = sideOfKind(t.kind);
                    defBasis = QStringLiteral("fixed-track");
                    break;
                }
            }
        }
        // Az adatok ítélete a megerősített sorokból (Mixed nem számít).
        const int lr = p.localLines + p.remoteLines;
        Side dataSide = Side::Unknown;
        float maj = 0.0f;
        if (lr >= sides::kPersonMinLines) {
            maj = float(std::max(p.localLines, p.remoteLines)) / float(lr);
            dataSide = maj >= sides::kPersonMajority
                ? (p.localLines >= p.remoteLines ? Side::Local : Side::Remote)
                : Side::Mixed;
        }
        if (p.defaultSide != Side::Unknown) {
            if (opposite(dataSide, p.defaultSide) && lr >= sides::kOverrideMinLines
                && maj >= sides::kOverrideMajority) {
                p.side = dataSide;
                p.confidence = maj;
                p.basis = QStringLiteral("override");
                qCInfo(lcApp).noquote()
                    << QStringLiteral("Sáv-oldal: %1 alapból %2 (%3), de a megerősített sorai szerint %4 (%5/%6) — felülírva.")
                           .arg(p.displayName, sideName(p.defaultSide), defBasis, sideName(dataSide))
                           .arg(std::max(p.localLines, p.remoteLines)).arg(lr);
            } else {
                p.side = p.defaultSide;
                p.confidence = dataSide == p.defaultSide ? maj : sides::kDefaultConfidence;
                p.basis = defBasis;
            }
        } else if (dataSide != Side::Unknown) {
            p.side = dataSide;
            p.confidence = maj;
            p.basis = QStringLiteral("lines");
        } else {
            p.basis = QStringLiteral("none");
        }
    }

    // Ellentmondások: a sor egyértelműen a beszélője oldalával ellentétes oldalon szólt.
    for (const LineSide& ls : r.lines) {
        const PersonSide& p = r.persons.at(personIdx.value(ls.speakerKey));
        if (!opposite(ls.side, p.side)) continue;
        r.conflicts.append({ls.utteranceId, ls.startMs, ls.speakerKey, ls.side, p.side, ls.localShare});
    }
    std::stable_sort(r.conflicts.begin(), r.conflicts.end(),
                     [](const SideConflict& a, const SideConflict& b) { return a.startMs < b.startMs; });
    return r;
}

} // namespace tanara
