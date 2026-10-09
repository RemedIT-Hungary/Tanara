#include "tanara/store/VoiceprintStore.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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

namespace tanara {

namespace {

QString findPersonByName(const QMap<QString, QVector<Voiceprint>>& people, const QString& name)
{
    // Case-insensitive kulcs-keresés (a tárolt írásmódot adja vissza).
    for (auto it = people.constBegin(); it != people.constEnd(); ++it)
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return it.key();
    return QString();
}

} // namespace

VoiceprintStore::VoiceprintStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? paths::metadataFile(QStringLiteral("voiceprints.json"))
        : filePath;
    load();
}

QStringList VoiceprintStore::people() const
{
    QStringList names = m_people.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QVector<Voiceprint> VoiceprintStore::printsFor(const QString& name) const
{
    const QString key = findPersonByName(m_people, name.trimmed());
    return key.isEmpty() ? QVector<Voiceprint>() : m_people.value(key);
}

int VoiceprintStore::printCount(const QString& name) const
{
    return printsFor(name).size();
}

int VoiceprintStore::totalPrintCount() const
{
    int n = 0;
    for (const auto& v : m_people)
        n += v.size();
    return n;
}

void VoiceprintStore::addPrint(const QString& name, Voiceprint print)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || print.embedding.isEmpty())
        return;
    if (print.id.isEmpty())
        print.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    print.dim = print.embedding.size();
    if (print.model.isEmpty())
        print.model = VoiceModelRegistry::defaultModelId();
    // Biztos, ami biztos: normalizálva tároljuk (a cosine így stabil marad).
    print.embedding = l2normalize(print.embedding);

    // Zár + a lemez friss állapota: más folyamat időközben felvett lenyomatai megmaradnak.
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const QString key = findPersonByName(m_people, n);
    if (key.isEmpty())
        m_people[n].append(print);
    else
        m_people[key].append(print);
    persist();
}

bool VoiceprintStore::removePrint(const QString& printId)
{
    if (printId.isEmpty())
        return false;
    const SharedFileLock lock(m_filePath);
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
    // Üresen maradt személyt kuka.
    for (auto it = m_people.begin(); it != m_people.end(); ) {
        if (it.value().isEmpty()) it = m_people.erase(it);
        else ++it;
    }
    if (removed)
        persist();
    return removed;
}

bool VoiceprintStore::findPrint(const QString& printId, QString* owner, Voiceprint* print) const
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

void VoiceprintStore::renamePerson(const QString& oldName, const QString& newName)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o.compare(n, Qt::CaseInsensitive) == 0)
        return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const QString srcKey = findPersonByName(m_people, o);
    if (srcKey.isEmpty())
        return;
    const QVector<Voiceprint> prints = m_people.take(srcKey);
    const QString dstKey = findPersonByName(m_people, n);
    if (dstKey.isEmpty())
        m_people[n] = prints;
    else
        m_people[dstKey] += prints;   // egyesítés, ha a cél már létezik
    persist();
}

void VoiceprintStore::removePerson(const QString& name)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const QString key = findPersonByName(m_people, name.trimmed());
    if (!key.isEmpty() && m_people.remove(key) > 0)
        persist();
}

void VoiceprintStore::merge(const QString& from, const QString& into)
{
    const QString f = from.trimmed(), i = into.trimmed();
    if (f.isEmpty() || i.isEmpty() || f.compare(i, Qt::CaseInsensitive) == 0)
        return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const QString fKey = findPersonByName(m_people, f);
    if (fKey.isEmpty())
        return;
    const QVector<Voiceprint> prints = m_people.take(fKey);
    const QString iKey = findPersonByName(m_people, i);
    if (iKey.isEmpty())
        m_people[i] = prints;
    else
        m_people[iKey] += prints;
    persist();
}

namespace {

// Egy személy pontszáma: modellenként a max cosine az adott modellű lenyomatain, majd átlag a
// közös modelleken. Nincs közös modell → -1.
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
        if (best < -1.5) continue;   // ennek a modellnek nincs lenyomata a személynél
        sum += best;
        ++n;
    }
    return n > 0 ? sum / n : -1.0;
}

EmbeddingSet defaultSet(const QVector<float>& embedding)
{
    EmbeddingSet set;
    if (!embedding.isEmpty()) set.insert(VoiceModelRegistry::defaultModelId(), embedding);
    return set;
}

} // namespace

QVector<VoiceMatch> VoiceprintStore::rankedMatches(const EmbeddingSet& query,
                                                   const QStringList& modelIds) const
{
    QVector<VoiceMatch> out;
    const QStringList ids = VoiceModelRegistry::normalizeIds(modelIds);
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
    return bestMatch(defaultSet(embedding), {VoiceModelRegistry::defaultModelId()});
}

QVector<VoiceMatch> VoiceprintStore::rankedMatches(const QVector<float>& embedding) const
{
    return rankedMatches(defaultSet(embedding), {VoiceModelRegistry::defaultModelId()});
}

QStringList VoiceprintStore::printsMissingModel(const QString& name, const QString& modelId) const
{
    QStringList refs, have;
    for (const Voiceprint& p : printsFor(name)) {
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
    QHash<QString, QVector<int>> byKey;   // kulcs → a csoportok indexei
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

QVector<Voiceprint> VoiceprintStore::siblingsOf(const QString& printId, QString* owner) const
{
    QString who;
    Voiceprint print;
    if (!findPrint(printId, &who, &print)) return {};
    if (owner) *owner = who;
    for (const QVector<Voiceprint>& g : siblingGroups(printsFor(who)))
        for (const Voiceprint& p : g)
            if (p.id == printId) return g;
    return {print};
}

QVector<Voiceprint> VoiceprintStore::samplesMissingModel(const QString& name, const QString& modelId) const
{
    QVector<Voiceprint> out;
    for (const QVector<Voiceprint>& g : siblingGroups(printsFor(name))) {
        if (g.first().sampleRef.isEmpty()) continue;   // nincs mintája: nem pótolható
        bool has = false;
        for (const Voiceprint& p : g)
            if (p.model == modelId) { has = true; break; }
        if (!has) out.append(g.first());
    }
    return out;
}

int VoiceprintStore::sampleCount(const QString& name) const
{
    return int(siblingGroups(printsFor(name)).size());
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
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Értelmezhetetlen fájl: a memóriabeli lenyomatok maradnak, a fájlt a következő
        // mentés félreteszi (a lenyomatokat nem írjuk felül nyom nélkül).
        qCWarning(lcVoice).noquote() << "voiceprints.json nem értelmezhető:" << m_filePath << err.errorString();
        m_corrupt = true;
        return;
    }
    const QJsonArray people = doc.object().value(QStringLiteral("people")).toArray();
    m_people.clear();
    for (const QJsonValue& pv : people) {
        const QJsonObject po = pv.toObject();
        const QString name = po.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        QVector<Voiceprint> prints;
        const QJsonArray arr = po.value(QStringLiteral("prints")).toArray();
        for (const QJsonValue& vv : arr) {
            const QJsonObject o = vv.toObject();
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
            vp.model = o.value(QStringLiteral("model")).toString(VoiceModelRegistry::defaultModelId());
            if (vp.model.isEmpty()) vp.model = VoiceModelRegistry::defaultModelId();
            for (const QJsonValue& r : o.value(QStringLiteral("sourceRefs")).toArray())
                if (!r.toString().isEmpty()) vp.sourceRefs << r.toString();
            if (!vp.embedding.isEmpty())
                prints.append(vp);
        }
        if (!prints.isEmpty())
            m_people[name] = prints;
    }
}

void VoiceprintStore::persist()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcVoice).noquote() << "voiceprints.json: a sérült fájl nem tehető félre — a mentés kimarad:"
                                     << m_filePath;
        return;
    }
    m_corrupt = false;
    QJsonArray people;
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it) {
        QJsonObject po;
        po[QStringLiteral("name")] = it.key();
        QJsonArray prints;
        for (const Voiceprint& vp : it.value()) {
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
            prints.append(o);
        }
        po[QStringLiteral("prints")] = prints;
        people.append(po);
    }
    QJsonObject root;
    root[QStringLiteral("people")] = people;
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (f.commit())
            m_stamp = FileStamp::of(m_filePath);   // a saját írásunkat nem kell visszaolvasni
    }
}

} // namespace tanara
