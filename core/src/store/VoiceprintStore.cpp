#include "tanara/store/VoiceprintStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
        ? QDir(QDir::homePath()).filePath(QStringLiteral(".tanara/voiceprints.json"))
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
    // Biztos, ami biztos: normalizálva tároljuk (a cosine így stabil marad).
    print.embedding = l2normalize(print.embedding);

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

void VoiceprintStore::renamePerson(const QString& oldName, const QString& newName)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o.compare(n, Qt::CaseInsensitive) == 0)
        return;
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
    const QString key = findPersonByName(m_people, name.trimmed());
    if (!key.isEmpty() && m_people.remove(key) > 0)
        persist();
}

void VoiceprintStore::merge(const QString& from, const QString& into)
{
    const QString f = from.trimmed(), i = into.trimmed();
    if (f.isEmpty() || i.isEmpty() || f.compare(i, Qt::CaseInsensitive) == 0)
        return;
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

VoiceMatch VoiceprintStore::bestMatch(const QVector<float>& embedding) const
{
    VoiceMatch best;   // { "", -1 }
    if (embedding.isEmpty() || m_people.isEmpty())
        return best;
    const QVector<float> q = l2normalize(embedding);
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it) {
        double personBest = -2.0;
        for (const Voiceprint& p : it.value()) {
            const double s = cosineSimilarity(q, p.embedding);
            if (s > personBest)
                personBest = s;
        }
        if (personBest > best.score) {
            best.score = personBest;
            best.name = it.key();
        }
    }
    return best;
}

QVector<VoiceMatch> VoiceprintStore::rankedMatches(const QVector<float>& embedding) const
{
    QVector<VoiceMatch> out;
    if (embedding.isEmpty() || m_people.isEmpty())
        return out;
    const QVector<float> q = l2normalize(embedding);
    for (auto it = m_people.constBegin(); it != m_people.constEnd(); ++it) {
        double personBest = -2.0;
        for (const Voiceprint& p : it.value())
            personBest = std::max(personBest, cosineSimilarity(q, p.embedding));
        out.append(VoiceMatch{it.key(), personBest});
    }
    std::sort(out.begin(), out.end(),
              [](const VoiceMatch& a, const VoiceMatch& b) { return a.score > b.score; });
    return out;
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

void VoiceprintStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray people =
        QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("people")).toArray();
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
            if (!vp.embedding.isEmpty())
                prints.append(vp);
        }
        if (!prints.isEmpty())
            m_people[name] = prints;
    }
}

void VoiceprintStore::persist() const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
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
        f.commit();
    }
}

} // namespace tanara
