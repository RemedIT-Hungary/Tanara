#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/domain/Voice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace tanara::library {

VoiceprintStore::VoiceprintStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? paths::current().dataFile(paths::files::Voiceprints)
        : filePath;
    load();
}

PersonIds VoiceprintStore::people() const
{
    return PersonIds(m_people.keyBegin(), m_people.keyEnd());
}

QVector<Voiceprint> VoiceprintStore::printsFor(const PersonId& person) const
{
    return m_people.value(person);
}

int VoiceprintStore::printCount(const PersonId& person) const
{
    return printsFor(person).size();
}

int VoiceprintStore::totalPrintCount() const
{
    int n = 0;
    for (const auto& v : m_people)
        n += v.size();
    return n;
}

bool VoiceprintStore::acquired(const SharedFileLock& lock, const char* operation)
{
    m_lockFailed = !lock.locked();
    if (m_lockFailed)
        qCWarning(lcStore).noquote()
            << QStringLiteral("voiceprints.json %1: another process holds the lock, the change was not written: %2")
                   .arg(QLatin1String(operation), m_filePath);
    return !m_lockFailed;
}

// Every modification: lock -> the fresh state from disk -> modify -> write out. Without the lock nothing
// changes, neither in memory nor on disk (see PeopleStore).

bool VoiceprintStore::addPrint(const PersonId& person, Voiceprint print)
{
    if (person.isNull() || print.embedding.isEmpty())
        return false;
    if (print.id.isEmpty())
        print.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    print.dim = print.embedding.size();
    if (print.model.isEmpty())
        print.model = voicemodels::defaultModelId();
    // To be safe we store it normalized (so the cosine stays stable).
    print.embedding = l2normalize(print.embedding);

    // Lock + the fresh state from disk: voiceprints another process added meanwhile are kept.
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "addPrint")) return false;
    reloadIfChanged();
    if (m_legacy) return false;
    m_people[person].append(print);
    persist();
    return true;
}

bool VoiceprintStore::removePrint(const QString& printId)
{
    if (printId.isEmpty())
        return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "removePrint")) return false;
    reloadIfChanged();
    bool removed = false;
    for (auto it = m_people.begin(); it != m_people.end(); ++it) {
        auto& prints = it.value();
        const int before = prints.size();
        prints.erase(std::remove_if(prints.begin(), prints.end(),
                         [&](const Voiceprint& p) { return p.id == printId; }),
                     prints.end());
        if (prints.size() != before)
            removed = true;
    }
    // A person left empty goes to the bin.
    for (auto it = m_people.begin(); it != m_people.end(); ) {
        if (it.value().isEmpty()) it = m_people.erase(it);
        else ++it;
    }
    if (removed)
        persist();
    return removed;
}

bool VoiceprintStore::findPrint(const QString& printId, PersonId* owner, Voiceprint* print) const
{
    if (printId.isEmpty()) return false;
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it)
        for (const Voiceprint& p : it.value()) {
            if (p.id != printId) continue;
            if (owner) *owner = it.key();
            if (print) *print = p;
            return true;
        }
    return false;
}

void VoiceprintStore::removePerson(const PersonId& person)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "removePerson")) return;
    reloadIfChanged();
    if (m_people.remove(person) > 0)
        persist();
}

bool VoiceprintStore::movePrint(const QString& printId, const PersonId& to)
{
    if (printId.isEmpty() || to.isNull())
        return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "movePrint")) return false;
    reloadIfChanged();
    if (m_legacy) return false;
    for (auto it = m_people.begin(); it != m_people.end(); ++it) {
        QVector<Voiceprint>& prints = it.value();
        const auto found = std::find_if(prints.begin(), prints.end(),
                                        [&](const Voiceprint& p) { return p.id == printId; });
        if (found == prints.end()) continue;
        if (it.key() == to) return true;   // already there
        const Voiceprint moved = *found;
        prints.erase(found);
        if (prints.isEmpty()) m_people.erase(it);   // a person left empty goes to the bin
        m_people[to].append(moved);
        persist();
        return true;
    }
    return false;
}

QStringList VoiceprintStore::merge(const PersonId& from, const PersonId& into)
{
    QStringList moved;
    if (from.isNull() || into.isNull() || from == into)
        return moved;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "merge")) return moved;
    reloadIfChanged();
    if (!m_people.contains(from))
        return moved;
    const QVector<Voiceprint> prints = m_people.take(from);
    for (const Voiceprint& p : prints) moved << p.id;
    m_people[into] += prints;
    persist();
    return moved;
}

namespace {

// One person's score: per model the max cosine over their voiceprints of that model, then the mean
// over the shared models. No shared model -> -1.
double personScore(const QVector<Voiceprint>& prints, const EmbeddingSet& query, const QStringList& ids)
{
    double sum = 0.0;
    int n = 0;
    for (const QString& id : ids) {
        const QVector<float> q = query.value(id);
        if (q.isEmpty()) continue;
        double best = -2.0;
        for (const Voiceprint& p : prints)
            if (p.model == id)
                best = std::max(best, VoiceprintStore::cosineSimilarity(q, p.embedding));
        if (best < -1.5) continue;   // this model has no voiceprint for the person
        sum += best;
        ++n;
    }
    return n > 0 ? sum / n : -1.0;
}

EmbeddingSet defaultSet(const QVector<float>& embedding)
{
    EmbeddingSet set;
    if (!embedding.isEmpty()) set.insert(voicemodels::defaultModelId(), embedding);
    return set;
}

} // namespace

QVector<VoiceMatch> VoiceprintStore::rankedMatches(const EmbeddingSet& query,
                                                   const QStringList& modelIds) const
{
    QVector<VoiceMatch> out;
    const QStringList ids = voicemodels::normalizeIds(modelIds);
    bool any = false;
    for (const QString& id : ids) any = any || !query.value(id).isEmpty();
    if (!any || m_people.isEmpty())
        return out;
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it)
        out.append(VoiceMatch{it.key(), personScore(it.value(), query, ids)});
    std::stable_sort(out.begin(), out.end(),
                     [](const VoiceMatch& a, const VoiceMatch& b) { return a.score > b.score; });
    return out;
}

VoiceMatch VoiceprintStore::bestMatch(const EmbeddingSet& query, const QStringList& modelIds) const
{
    const QVector<VoiceMatch> ranked = rankedMatches(query, modelIds);
    if (ranked.isEmpty() || ranked.first().score <= -1.0)
        return VoiceMatch();   // { "", -1 }
    return ranked.first();
}

VoiceMatch VoiceprintStore::bestMatch(const QVector<float>& embedding) const
{
    return bestMatch(defaultSet(embedding), {voicemodels::defaultModelId()});
}

QVector<VoiceMatch> VoiceprintStore::rankedMatches(const QVector<float>& embedding) const
{
    return rankedMatches(defaultSet(embedding), {voicemodels::defaultModelId()});
}

QStringList VoiceprintStore::printsMissingModel(const PersonId& person, const QString& modelId) const
{
    QStringList refs, have;
    for (const Voiceprint& p : printsFor(person)) {
        if (p.sampleRef.isEmpty()) continue;
        if (p.model == modelId) have << p.sampleRef;
        else if (!refs.contains(p.sampleRef)) refs << p.sampleRef;
    }
    QStringList out;
    for (const QString& r : std::as_const(refs))
        if (!have.contains(r)) out << r;
    out.sort();
    return out;
}

QString VoiceprintStore::siblingKey(const Voiceprint& p)
{
    return p.sampleRef + QChar(0x1f) + p.sourceMeetingId + QChar(0x1f) + p.createdAt;
}

QVector<QVector<Voiceprint>> VoiceprintStore::siblingGroups(const QVector<Voiceprint>& prints)
{
    QVector<QVector<Voiceprint>> groups;
    QHash<QString, QVector<int>> byKey;   // key -> the groups' indexes
    for (const Voiceprint& p : prints) {
        if (p.sampleRef.isEmpty()) { groups.append(QVector<Voiceprint>{p}); continue; }
        QVector<int>& idx = byKey[siblingKey(p)];
        int target = -1;
        for (int g : std::as_const(idx)) {
            bool hasModel = false;
            for (const Voiceprint& q : std::as_const(groups[g]))
                if (q.model == p.model) { hasModel = true; break; }
            if (!hasModel) { target = g; break; }
        }
        if (target < 0) { target = int(groups.size()); groups.append(QVector<Voiceprint>()); idx.append(target); }
        groups[target].append(p);
    }
    for (QVector<Voiceprint>& g : groups)
        std::stable_sort(g.begin(), g.end(), [](const Voiceprint& a, const Voiceprint& b) { return a.model < b.model; });
    return groups;
}

QVector<Voiceprint> VoiceprintStore::siblingsOf(const QString& printId, PersonId* owner) const
{
    PersonId who;
    Voiceprint print;
    if (!findPrint(printId, &who, &print)) return {};
    if (owner) *owner = who;
    for (const QVector<Voiceprint>& g : siblingGroups(printsFor(who)))
        for (const Voiceprint& p : g)
            if (p.id == printId) return g;
    return {print};
}

QVector<Voiceprint> VoiceprintStore::samplesMissingModel(const PersonId& person, const QString& modelId) const
{
    QVector<Voiceprint> out;
    for (const QVector<Voiceprint>& g : siblingGroups(printsFor(person))) {
        if (g.first().sampleRef.isEmpty()) continue;   // no sample: cannot be backfilled
        bool has = false;
        for (const Voiceprint& p : g)
            if (p.model == modelId) { has = true; break; }
        if (!has) out.append(g.first());
    }
    return out;
}

int VoiceprintStore::sampleCount(const PersonId& person) const
{
    return int(siblingGroups(printsFor(person)).size());
}

double VoiceprintStore::cosineSimilarity(const QVector<float>& a, const QVector<float>& b)
{
    if (a.isEmpty() || a.size() != b.size())
        return 0.0;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na  += static_cast<double>(a[i]) * a[i];
        nb  += static_cast<double>(b[i]) * b[i];
    }
    if (na <= 0.0 || nb <= 0.0)
        return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

QVector<float> VoiceprintStore::l2normalize(const QVector<float>& v)
{
    double n = 0.0;
    for (float x : v)
        n += static_cast<double>(x) * x;
    if (n <= 0.0)
        return v;
    const double inv = 1.0 / std::sqrt(n);
    QVector<float> out;
    out.reserve(v.size());
    for (float x : v)
        out.append(static_cast<float>(x * inv));
    return out;
}

// ---- persistence ----------------------------------------------------------

void VoiceprintStore::reloadIfChanged()
{
    if (FileStamp::of(m_filePath) != m_stamp)
        load();
}

void VoiceprintStore::load()
{
    m_stamp = FileStamp::of(m_filePath);
    m_corrupt = false;
    m_legacy = false;
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Unparseable file: the in-memory voiceprints stay, the next save sets the file aside (we do not
        // overwrite the voiceprints without a trace).
        qCWarning(lcVoice).noquote() << "voiceprints.json cannot be parsed:" << m_filePath << err.errorString();
        m_corrupt = true;
        return;
    }
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(0);
    const QJsonValue peopleValue = root.value(QStringLiteral("people"));
    m_people.clear();
    if (version < kFormatVersion || !peopleValue.isObject()) {
        m_legacy = true;
        qCCritical(lcVoice).noquote()
            << QStringLiteral("voiceprints.json is in an old, name-keyed format (version %1); the app does not read it."
                              " Convert it once (with a backup): run: tanara-cli migrate person-ids  (%2)")
                   .arg(version).arg(m_filePath);
        return;
    }
    const QJsonObject people = peopleValue.toObject();
    for (auto pit = people.constBegin(); pit != people.constEnd(); ++pit) {
        const PersonId person = PersonId::fromString(pit.key());
        if (person.isNull())
            continue;
        QVector<Voiceprint> prints;
        for (const QJsonValue& vv : pit.value().toArray()) {
            const Voiceprint vp = voiceprintFromJson(vv.toObject());
            if (!vp.embedding.isEmpty())
                prints.append(vp);
        }
        if (!prints.isEmpty())
            m_people[person] = prints;
    }
}

void VoiceprintStore::persist()
{
    if (m_legacy) {
        qCCritical(lcVoice).noquote() << "voiceprints.json: old format on disk, not overwritten — run: tanara-cli migrate person-ids";
        return;
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcVoice).noquote() << "voiceprints.json: the corrupt file cannot be set aside — save skipped:"
                                     << m_filePath;
        return;
    }
    m_corrupt = false;
    QJsonObject people;
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it) {
        QJsonArray prints;
        for (const Voiceprint& vp : it.value())
            prints.append(voiceprintToJson(vp));
        people.insert(it.key().toString(), prints);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("people")] = people;
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (f.commit())
            m_stamp = FileStamp::of(m_filePath);   // no need to reread our own write
    }
}

QJsonObject voiceprintToJson(const Voiceprint& vp)
{
    QJsonObject o;
    o[QStringLiteral("id")] = vp.id;
    QJsonArray emb;
    for (float x : vp.embedding)
        emb.append(static_cast<double>(x));
    o[QStringLiteral("embedding")] = emb;
    o[QStringLiteral("dim")] = vp.dim;
    o[QStringLiteral("sourceMeetingId")] = vp.sourceMeetingId;
    o[QStringLiteral("sourceTrack")] = vp.sourceTrack;
    o[QStringLiteral("device")] = vp.device;
    o[QStringLiteral("sampleRef")] = vp.sampleRef;
    o[QStringLiteral("createdAt")] = vp.createdAt;
    o[QStringLiteral("model")] = vp.model;
    if (!vp.sourceRefs.isEmpty())
        o[QStringLiteral("sourceRefs")] = QJsonArray::fromStringList(vp.sourceRefs);
    return o;
}

Voiceprint voiceprintFromJson(const QJsonObject& o)
{
    Voiceprint vp;
    vp.id = o.value(QStringLiteral("id")).toString();
    const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
    vp.embedding.reserve(emb.size());
    for (const QJsonValue& e : emb)
        vp.embedding.append(static_cast<float>(e.toDouble()));
    vp.dim = o.value(QStringLiteral("dim")).toInt(vp.embedding.size());
    vp.sourceMeetingId = o.value(QStringLiteral("sourceMeetingId")).toString();
    vp.sourceTrack = o.value(QStringLiteral("sourceTrack")).toString();
    vp.device = o.value(QStringLiteral("device")).toString();
    vp.sampleRef = o.value(QStringLiteral("sampleRef")).toString();
    vp.createdAt = o.value(QStringLiteral("createdAt")).toString();
    vp.model = o.value(QStringLiteral("model")).toString(voicemodels::defaultModelId());
    if (vp.model.isEmpty()) vp.model = voicemodels::defaultModelId();
    for (const QJsonValue& r : o.value(QStringLiteral("sourceRefs")).toArray())
        if (!r.toString().isEmpty()) vp.sourceRefs << r.toString();
    return vp;
}

} // namespace tanara::library