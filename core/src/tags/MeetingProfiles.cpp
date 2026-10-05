#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagNames.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/library/TextFold.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/SharedFile.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace tanara {

// ---- szövegfeldolgozás -------------------------------------------------------------------

namespace tagtext {

namespace {

// A levágható toldalékok hajtogatott alakban (a -ből/-ről/-höz/-ön/-ök/-öt/-ünk alakok a
// hajtogatás után egybeesnek a párjukkal). Hosszabb elöl, hogy a leghosszabb illeszkedjen.
const QStringList& suffixes()
{
    static const QStringList list = [] {
        QStringList s{
            QStringLiteral("ban"), QStringLiteral("ben"), QStringLiteral("nak"), QStringLiteral("nek"),
            QStringLiteral("val"), QStringLiteral("vel"), QStringLiteral("bol"), QStringLiteral("rol"),
            QStringLiteral("hoz"), QStringLiteral("hez"), QStringLiteral("ba"),  QStringLiteral("be"),
            QStringLiteral("ra"),  QStringLiteral("re"),  QStringLiteral("on"),  QStringLiteral("en"),
            QStringLiteral("t"),   QStringLiteral("k"),   QStringLiteral("i"),   QStringLiteral("ja"),
            QStringLiteral("je"),  QStringLiteral("ok"),  QStringLiteral("ek"),  QStringLiteral("unk"),
            QStringLiteral("nal"), QStringLiteral("nel"), QStringLiteral("ig"),  QStringLiteral("ert"),
            QStringLiteral("kat"), QStringLiteral("ket"), QStringLiteral("at"),  QStringLiteral("et"),
            QStringLiteral("ot")};
        std::stable_sort(s.begin(), s.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        return s;
    }();
    return list;
}

const QSet<QString>& stopWords()
{
    static const QSet<QString> words = [] {
        QSet<QString> s;
        for (const char* w : {
                 // magyar névelők, kötőszók, névmások, határozószók, töltelékszavak
                 "a", "az", "egy", "es", "is", "de", "hogy", "nem", "sem", "se", "meg", "mar", "csak",
                 "van", "vannak", "volt", "voltak", "lesz", "lenne", "lett", "lehet", "kell", "kellene",
                 "igen", "akkor", "most", "itt", "ott", "mert", "vagy", "mint", "majd", "nagyon",
                 "szoval", "tehat", "ugye", "persze", "jo", "jol", "hat", "na", "oke", "okes", "okay",
                 "aha", "aham", "mhm", "hmm", "ilyen", "olyan", "amit", "ami", "aki", "akik", "amik",
                 "ahol", "amikor", "amely", "amelyik", "valami", "valamit", "valami", "minden",
                 "mindent", "ezzel", "azzal", "ennek", "annak", "neki", "nekem", "nekunk", "nektek",
                 "nekik", "engem", "teged", "minket", "titeket", "oket", "ezt", "azt", "ez", "ezek",
                 "azok", "ezeket", "azokat", "ezen", "azon", "ebben", "abban", "erre", "arra", "errol",
                 "arrol", "ebbol", "abbol", "ehhez", "ahhoz", "itt", "igy", "ugy", "ugyanaz", "szerintem",
                 "gondolom", "tudom", "tudod", "tudja", "tudjuk", "mondjuk", "mondom", "latod", "kicsit",
                 "egyebkent", "valahogy", "valamint", "azert", "ezert", "mikor", "miert", "hogyan",
                 "honnan", "hova", "hol", "mennyi", "mennyire", "sok", "sokat", "keves", "megint",
                 "aztan", "utana", "elott", "utan", "alatt", "felett", "kozott", "szamara", "rola",
                 "benne", "vele", "tole", "hozza", "nala", "illetve", "pedig", "hanem", "habar", "bar",
                 "viszont", "tovabba", "illetoleg", "meg", "mindig", "soha", "sose", "neha", "eleg",
                 "kb", "kozben", "talan", "biztos", "biztosan", "valoban", "teljesen", "eppen", "pont",
                 "pontosan", "mindenkinek", "mindenki", "senki", "semmi", "semmit", "barmi", "barmit",
                 "nincs", "nincsen", "vagyok", "vagy", "vagyunk", "vagytok", "volna", "leszek", "lesznek",
                 "csinal", "csinalni", "csinaljuk", "megy", "menni", "mehet", "jon", "jonni", "kerdes",
                 "dolog", "dolgot", "dolgok", "resz", "reszt", "ido", "idot", "eloszor", "masodszor",
                 "elso", "masodik", "harmadik", "egyik", "masik", "tobbi", "tobb", "kevesebb", "nagy",
                 "kis", "kicsi", "uj", "regi", "szia", "sziasztok", "halo", "hallo", "koszonom",
                 "koszi", "koszonjuk", "kerem", "kerlek", "elnezest", "bocsi", "bocsanat", "rendben",
                 "rendben", "jolvan", "nahat", "ja", "jaja", "nemtom", "asszem", "szerintunk", "mi",
                 "ti", "en", "te", "o", "ok", "maga", "maguk", "magunk", "sajat", "ilyenkor", "olyankor",
                 "akar", "akarok", "akarunk", "szeretnem", "szeretnek", "szeretnenk", "tudunk", "tudok",
                 "tudsz", "tud", "fog", "fogok", "fogunk", "fogja", "fogjuk", "nezd", "nezzuk", "figyelj",
                 "egyszer", "ketszer", "egyszeruen", "alapvetoen", "lenyegeben", "gyakorlatilag",
                 "konkretan", "tulajdonkeppen", "valojaban", "amugy", "egyebkent", "eleve", "inkabb",
                 "nagyjabol", "kozul", "fele", "fel", "le", "ki", "be", "el", "ra", "at", "ossze",
                 "vissza", "tovabb", "mostanaban", "jelenleg", "holnap", "tegnap", "ma", "most",
                 "het", "heten", "hetre", "napot", "napon", "ora", "oraban", "perc", "percet",
                 // angol
                 "the", "and", "that", "this", "with", "for", "you", "are", "have", "has", "had", "was",
                 "were", "not", "but", "what", "all", "can", "will", "just", "yeah", "yes", "okay",
                 "so", "then", "there", "here", "they", "them", "their", "from", "about", "would",
                 "could", "should", "which", "when", "where", "who", "how", "why", "our", "your",
                 "its", "it's", "into", "also", "like", "know", "think", "really", "very", "some",
                 "any", "one", "two", "get", "got", "going", "gonna", "well", "right", "maybe" })
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return words;
}

} // namespace

QString stem(const QString& w)
{
    for (const QString& suf : suffixes()) {
        if (w.size() - suf.size() < 4) continue;
        if (w.endsWith(suf)) return w.left(w.size() - suf.size());
    }
    return w;
}

bool isStopWord(const QString& w) { return stopWords().contains(w); }

QVector<QPair<QString, QString>> terms(const QString& text)
{
    static const QRegularExpression splitRe(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QVector<QPair<QString, QString>> out;
    const QString nfc = textfold::normalize(text);
    for (const QString& tok : nfc.split(splitRe, Qt::SkipEmptyParts)) {
        if (tok.size() < 3) continue;
        if (std::any_of(tok.cbegin(), tok.cend(), [](QChar c) { return c.isDigit(); })) continue;
        const QString folded = textfold::fold(tok);
        if (isStopWord(folded)) continue;
        const QString s = stem(folded);
        if (s.size() < 3 || isStopWord(s)) continue;
        out.append({ s, tok });
    }
    return out;
}

} // namespace tagtext

// ---- háttérszál: kifejezés-számlálás + profile.json ----------------------------------------

namespace {

constexpr int kMaxTerms = 60;
constexpr double kWeightParticipants = 0.45;
constexpr double kWeightTerms = 0.40;
constexpr double kWeightTitle = 0.15;
const QString kProfileFile = QStringLiteral("profile.json");

// Egy meeting nyers kifejezés-számai: tő → (előfordulás, leggyakoribb szóalak).
struct RawTerms {
    QHash<QString, int> tf;
    QHash<QString, QString> surface;
};

QString segmentsPath(const QString& folder)
{
    return QDir(folder).filePath(QStringLiteral("transcript.segments.json"));
}

RawTerms countTerms(const QString& folder)
{
    RawTerms r;
    QHash<QString, QHash<QString, int>> forms;
    for (const TranscriptLine& l : speakeredit::loadTranscriptLines(folder))
        for (const auto& t : tagtext::terms(l.text)) {
            r.tf[t.first]++;
            forms[t.first][t.second]++;
        }
    for (auto it = forms.constBegin(); it != forms.constEnd(); ++it) {
        QString best;
        int bestN = -1;
        for (auto f = it->constBegin(); f != it->constEnd(); ++f) {
            // Gyakoribb nyer; egyenlőnél a nagybetűs (tulajdonnév), aztán a rövidebb.
            const bool better = f.value() > bestN
                || (f.value() == bestN && f.key().at(0).isUpper() && !best.at(0).isUpper())
                || (f.value() == bestN && f.key().at(0).isUpper() == best.at(0).isUpper() && f.key().size() < best.size());
            if (better) { best = f.key(); bestN = f.value(); }
        }
        r.surface.insert(it.key(), best);
    }
    return r;
}

bool readProfileFile(const QString& folder, const FileStamp& stamp, RawTerms* out)
{
    QFile f(QDir(folder).filePath(kProfileFile));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != 1) return false;
    const QJsonObject st = root.value(QStringLiteral("transcript")).toObject();
    if (qint64(st.value(QStringLiteral("mtimeMs")).toDouble()) != stamp.mtimeMs
        || qint64(st.value(QStringLiteral("size")).toDouble()) != stamp.size)
        return false;
    const QJsonObject terms = root.value(QStringLiteral("terms")).toObject();
    for (auto it = terms.constBegin(); it != terms.constEnd(); ++it) {
        const QJsonArray a = it.value().toArray();
        out->tf.insert(it.key(), a.at(0).toInt());
        out->surface.insert(it.key(), a.at(1).toString());
    }
    return true;
}

void writeProfileFile(const QString& folder, const FileStamp& stamp, const RawTerms& r)
{
    QJsonObject terms;
    for (auto it = r.tf.constBegin(); it != r.tf.constEnd(); ++it)
        terms.insert(it.key(), QJsonArray{ it.value(), r.surface.value(it.key()) });
    const QJsonObject root{
        { QStringLiteral("version"), 1 },
        { QStringLiteral("transcript"), QJsonObject{ { QStringLiteral("mtimeMs"), double(stamp.mtimeMs) },
                                                     { QStringLiteral("size"), double(stamp.size) } } },
        { QStringLiteral("terms"), terms } };
    QSaveFile f(QDir(folder).filePath(kProfileFile));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// Union-find a félrehallás-összevonáshoz.
struct UnionFind {
    QHash<QString, QString> parent;
    QString find(const QString& x) {
        QString r = x;
        while (parent.contains(r) && parent.value(r) != r) r = parent.value(r);
        QString c = x;   // útvonal-tömörítés
        while (parent.contains(c) && parent.value(c) != r) { const QString n = parent.value(c); parent[c] = r; c = n; }
        return r;
    }
    void unite(const QString& a, const QString& b) {
        if (!parent.contains(a)) parent.insert(a, a);
        if (!parent.contains(b)) parent.insert(b, b);
        const QString ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    }
};

double cosine(const QHash<QString, double>& a, double na, const QHash<QString, double>& b, double nb)
{
    if (na <= 0 || nb <= 0) return 0.0;
    const auto& small = a.size() < b.size() ? a : b;
    const auto& large = a.size() < b.size() ? b : a;
    double dot = 0;
    for (auto it = small.constBegin(); it != small.constEnd(); ++it) {
        const auto o = large.constFind(it.key());
        if (o != large.constEnd()) dot += it.value() * o.value();
    }
    return dot / (na * nb);
}

} // namespace

class ProfileWorker : public QObject {
public:
    std::atomic<bool> stop{false};
};

struct MeetingProfiles::Impl {
    MeetingProfiles* q = nullptr;
    MeetingStore* store = nullptr;
    QThread thread;
    ProfileWorker* worker = nullptr;

    struct Row {
        QString     title;
        QStringList titleWords;
        QStringList participants;
        QString     folder;
        bool        hasTranscript = false;
    };
    struct Built {
        FileStamp stamp;
        RawTerms  raw;
    };
    mutable bool rowsLoaded = false;
    mutable QHash<QString, Row> rows;
    QHash<QString, Built> built;
    QSet<QString> inFlight;

    // A könyvtár-szintű statisztika (lusta, a kész profilokból).
    mutable bool statsDirty = true;
    mutable QHash<QString, QString> canon;                 // nyers tő → összevont tő
    mutable QHash<QString, QString> display;               // összevont tő → megjelenített alak
    mutable QHash<QString, QHash<QString, double>> vec;    // meetingId → (tő → súly), ≤ 60
    mutable QHash<QString, double> norm;
    mutable QHash<QString, double> personWeight;           // név → ritkasági súly

    void ensureRows() const {
        if (rowsLoaded || !store) return;
        rows.clear();
        for (const Meeting& idx : store->loadAll()) {
            Meeting m = store->load(idx.id);
            if (m.id.isEmpty()) m = idx;
            if (m.folder.isEmpty()) m.folder = idx.folder;
            rows.insert(m.id, rowOf(m));
        }
        rowsLoaded = true;
        statsDirty = true;
    }
    static Row rowOf(const Meeting& m) {
        return Row{ m.title, meetingnotes::titleWords(m.title), MeetingLibrary::participantsOf(m),
                    m.folder, m.hasTranscript };
    }
    void reloadRow(const QString& id) {
        if (!rowsLoaded || !store) return;
        const Meeting m = store->load(id);
        if (m.id.isEmpty()) { rows.remove(id); built.remove(id); }
        else rows.insert(id, rowOf(m));
        statsDirty = true;
    }

    void computeStats() const {
        if (!statsDirty) return;
        statsDirty = false;
        ensureRows();
        canon.clear(); display.clear(); vec.clear(); norm.clear(); personWeight.clear();

        // Résztvevők ritkasága: aki a megbeszélések > 60 %-án ott van, ~0.
        const int nAll = int(rows.size());
        QHash<QString, int> pdf;
        for (const Row& r : std::as_const(rows))
            for (const QString& p : r.participants) pdf[p]++;
        for (auto it = pdf.constBegin(); it != pdf.constEnd(); ++it) {
            const double share = nAll > 0 ? double(it.value()) / nAll : 1.0;
            personWeight.insert(it.key(), std::max(0.0, 1.0 - share / 0.6));
        }

        // Kifejezések: nyers df + összesített előfordulás.
        QHash<QString, int> rawDf, rawTotal;
        QHash<QString, QHash<QString, int>> surfaces;   // nyers tő → (alak → db)
        int n = 0;
        for (auto it = built.constBegin(); it != built.constEnd(); ++it) {
            if (!rows.contains(it.key()) || it->raw.tf.isEmpty()) continue;
            ++n;
            for (auto t = it->raw.tf.constBegin(); t != it->raw.tf.constEnd(); ++t) {
                rawDf[t.key()]++;
                rawTotal[t.key()] += t.value();
                surfaces[t.key()][it->raw.surface.value(t.key())] += t.value();
            }
        }
        if (n == 0) return;

        // Félrehallás-összevonás: a ritka tövek közül az egy betűben eltérők (a hosszabbik
        // legalább 6, a rövidebb legalább 5 betű). A jelöltpárokat törlés-szomszédsággal
        // keressük (minden tő + egy-egy betűje nélküli alakjai), így nem kell minden párt
        // összevetni.
        const int rareDf = std::max(2, int(std::ceil(n * 0.1)));
        QHash<QString, QStringList> buckets;
        for (auto it = rawDf.constBegin(); it != rawDf.constEnd(); ++it) {
            const QString& s = it.key();
            if (it.value() > rareDf || s.size() < 5) continue;
            buckets[s].append(s);
            for (int i = 0; i < s.size(); ++i) buckets[s.left(i) + s.mid(i + 1)].append(s);
        }
        UnionFind uf;
        for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
            const QStringList& list = it.value();
            for (int i = 0; i < list.size(); ++i)
                for (int j = i + 1; j < list.size(); ++j) {
                    const QString& a = list.at(i);
                    const QString& b = list.at(j);
                    if (a == b || std::max(a.size(), b.size()) < 6) continue;
                    if (damerauLevenshtein(a, b, 1) <= 1) uf.unite(a, b);
                }
        }
        // Csoportonként a leggyakoribb tő lesz a közös.
        QHash<QString, QString> groupBest;
        for (auto it = uf.parent.constBegin(); it != uf.parent.constEnd(); ++it) {
            const QString root = uf.find(it.key());
            for (const QString& member : { it.key(), root }) {
                const QString cur = groupBest.value(root);
                if (cur.isEmpty() || rawTotal.value(member) > rawTotal.value(cur)
                    || (rawTotal.value(member) == rawTotal.value(cur) && member.size() > cur.size()))
                    groupBest[root] = member;
            }
        }
        for (auto it = rawDf.constBegin(); it != rawDf.constEnd(); ++it) {
            const QString s = it.key();
            canon.insert(s, uf.parent.contains(s) ? groupBest.value(uf.find(s)) : s);
        }

        // Megjelenített alak: az összevont csoport leggyakoribb eredeti írásmódja.
        QHash<QString, QHash<QString, int>> canonForms;
        for (auto it = surfaces.constBegin(); it != surfaces.constEnd(); ++it)
            for (auto f = it->constBegin(); f != it->constEnd(); ++f)
                canonForms[canon.value(it.key())][f.key()] += f.value();
        for (auto it = canonForms.constBegin(); it != canonForms.constEnd(); ++it) {
            QString best; int bestN = -1;
            for (auto f = it->constBegin(); f != it->constEnd(); ++f)
                if (f.value() > bestN || (f.value() == bestN && f.key() < best)) { best = f.key(); bestN = f.value(); }
            display.insert(it.key(), best);
        }

        // Összevont df, majd meetingenként tf × log(N / df), a legerősebb 60.
        QHash<QString, QHash<QString, int>> ctf;
        QHash<QString, int> df;
        for (auto it = built.constBegin(); it != built.constEnd(); ++it) {
            if (!rows.contains(it.key()) || it->raw.tf.isEmpty()) continue;
            QHash<QString, int>& m = ctf[it.key()];
            for (auto t = it->raw.tf.constBegin(); t != it->raw.tf.constEnd(); ++t)
                m[canon.value(t.key(), t.key())] += t.value();
            for (auto t = m.constBegin(); t != m.constEnd(); ++t) df[t.key()]++;
        }
        for (auto it = ctf.constBegin(); it != ctf.constEnd(); ++it) {
            QVector<QPair<double, QString>> w;
            for (auto t = it->constBegin(); t != it->constEnd(); ++t) {
                const double idf = std::log(double(n) / double(df.value(t.key(), 1)));
                if (idf <= 0) continue;
                w.append({ t.value() * idf, t.key() });
            }
            std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first > b.first;
                return a.second < b.second;
            });
            if (w.size() > kMaxTerms) w.resize(kMaxTerms);
            QHash<QString, double> v;
            double sq = 0;
            for (const auto& p : std::as_const(w)) { v.insert(p.second, p.first); sq += p.first * p.first; }
            vec.insert(it.key(), v);
            norm.insert(it.key(), std::sqrt(sq));
        }
    }

    double participantScore(const QStringList& a, const QStringList& b, QStringList* shared) const {
        double inter = 0, uni = 0;
        QSet<QString> all(a.cbegin(), a.cend());
        for (const QString& p : b) all.insert(p);
        QVector<QPair<double, QString>> common;
        for (const QString& p : std::as_const(all)) {
            const double w = personWeight.value(p, 1.0);
            uni += w;
            if (a.contains(p) && b.contains(p)) {
                inter += w;
                if (w > 0.05) common.append({ w, p });
            }
        }
        std::sort(common.begin(), common.end(), [](const auto& x, const auto& y) {
            if (x.first != y.first) return x.first > y.first;
            return x.second < y.second;
        });
        if (shared) for (const auto& c : std::as_const(common)) *shared << c.second;
        return uni > 0 ? inter / uni : 0.0;
    }

    QStringList shared(const QString& a, const QString& b, int limit) const {
        const QHash<QString, double> va = vec.value(a), vb = vec.value(b);
        QVector<QPair<double, QString>> common;
        for (auto it = va.constBegin(); it != va.constEnd(); ++it) {
            const auto o = vb.constFind(it.key());
            if (o != vb.constEnd()) common.append({ it.value() * o.value(), it.key() });
        }
        std::sort(common.begin(), common.end(), [](const auto& x, const auto& y) {
            if (x.first != y.first) return x.first > y.first;
            return x.second < y.second;
        });
        QStringList out;
        for (const auto& c : std::as_const(common)) {
            if (out.size() >= limit) break;
            out << display.value(c.second, c.second);
        }
        return out;
    }

    QVector<SimilarHit> rank(const QString& selfId, const QStringList& participants,
                             const QStringList& titleWords, int limit) const {
        computeStats();
        QVector<SimilarHit> hits;
        const bool hasVec = vec.contains(selfId);
        const QHash<QString, double> selfVec = vec.value(selfId);
        const double selfNorm = norm.value(selfId);
        for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) {
            if (it.key() == selfId) continue;
            QStringList sharedPeople;
            const double p = participantScore(participants, it->participants, &sharedPeople);
            const double t = hasVec && vec.contains(it.key())
                ? cosine(selfVec, selfNorm, vec.value(it.key()), norm.value(it.key())) : 0.0;
            const double ti = meetingnotes::titleSimilarity(titleWords, it->titleWords);
            const double score = kWeightParticipants * p + kWeightTerms * t + kWeightTitle * ti;
            if (score <= 0.0) continue;
            SimilarHit h;
            h.meetingId = it.key();
            h.score = score;
            if (p > 0 && !sharedPeople.isEmpty())
                h.reasons.append({ ReasonKind::Participant, sharedPeople.mid(0, 3) });
            if (t > 0) {
                const QStringList terms = shared(selfId, it.key(), 3);
                if (!terms.isEmpty()) h.reasons.append({ ReasonKind::Terms, terms });
            }
            if (ti > 0) h.reasons.append({ ReasonKind::Title, { it->title } });
            hits.append(h);
        }
        std::sort(hits.begin(), hits.end(), [](const SimilarHit& a, const SimilarHit& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.meetingId < b.meetingId;
        });
        if (limit >= 0 && hits.size() > limit) hits.resize(limit);
        return hits;
    }
};

MeetingProfiles::MeetingProfiles(MeetingStore* store, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->store = store;
    d->worker = new ProfileWorker;
    d->worker->moveToThread(&d->thread);
    connect(&d->thread, &QThread::finished, d->worker, &QObject::deleteLater);
    d->thread.setObjectName(QStringLiteral("tanara-profiles"));
    d->thread.start(QThread::LowPriority);
    if (store) {
        auto refresh = [this](const QString& id) { d->reloadRow(id); };
        connect(store, &MeetingStore::meetingUpdated, this, refresh);
        connect(store, &MeetingStore::meetingAdded, this, refresh);
        connect(store, &MeetingStore::meetingRemoved, this, [this](const QString& id) {
            d->rows.remove(id);
            d->built.remove(id);
            d->statsDirty = true;
        });
    }
}

MeetingProfiles::~MeetingProfiles()
{
    d->worker->stop = true;
    d->thread.quit();
    d->thread.wait();
}

void MeetingProfiles::ensureBuilt()
{
    d->ensureRows();
    for (auto it = d->rows.constBegin(); it != d->rows.constEnd(); ++it) {
        const QString id = it.key();
        if (!it->hasTranscript || it->folder.isEmpty() || d->inFlight.contains(id)) continue;
        const FileStamp stamp = FileStamp::of(segmentsPath(it->folder));
        if (stamp.mtimeMs < 0) continue;
        const auto b = d->built.constFind(id);
        if (b != d->built.constEnd() && b->stamp == stamp) continue;
        d->inFlight.insert(id);
        const QString folder = it->folder;
        ProfileWorker* worker = d->worker;
        QMetaObject::invokeMethod(worker, [this, worker, id, folder, stamp]() {
            if (worker->stop) return;
            RawTerms raw;
            if (!readProfileFile(folder, stamp, &raw)) {
                raw = countTerms(folder);
                writeProfileFile(folder, stamp, raw);
            }
            if (worker->stop) return;
            // Vissza a fő szálra (a profilok ott élnek). A MeetingProfiles a szál leállítása
            // előtt nem szűnik meg, így a cél-objektum itt még érvényes.
            QMetaObject::invokeMethod(this, [this, id, stamp, raw]() {
                d->inFlight.remove(id);
                if (d->rows.contains(id)) {
                    d->built.insert(id, Impl::Built{ stamp, raw });
                    d->statsDirty = true;
                    emit profileReady(id);
                }
                if (d->inFlight.isEmpty()) emit idle();
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }
}

bool MeetingProfiles::isIdle() const { return d->inFlight.isEmpty(); }

bool MeetingProfiles::isBuilt(const QString& meetingId) const { return d->built.contains(meetingId); }

void MeetingProfiles::invalidate(const QString& meetingId)
{
    d->built.remove(meetingId);
    d->reloadRow(meetingId);
    d->statsDirty = true;
}

QVector<SimilarHit> MeetingProfiles::similar(const QString& meetingId, int limit) const
{
    d->ensureRows();
    const auto self = d->rows.constFind(meetingId);
    if (self == d->rows.constEnd()) return {};
    return d->rank(meetingId, self->participants, self->titleWords, limit);
}

QVector<SimilarHit> MeetingProfiles::similarToDraft(const QString& title, const QStringList& participants,
                                                    const QString& excludeId, int limit) const
{
    d->ensureRows();
    const QString self = excludeId.isEmpty() ? QStringLiteral("\x01draft") : excludeId;
    return d->rank(self, participants, meetingnotes::titleWords(title), limit);
}

QStringList MeetingProfiles::termsOf(const QString& meetingId, int limit) const
{
    return topTerms({ meetingId }, limit);
}

QStringList MeetingProfiles::topTerms(const QStringList& meetingIds, int limit) const
{
    d->computeStats();
    QHash<QString, double> sum;
    QHash<QString, int> in;
    for (const QString& id : meetingIds) {
        const QHash<QString, double> v = d->vec.value(id);
        for (auto it = v.constBegin(); it != v.constEnd(); ++it) {
            sum[it.key()] += it.value();
            in[it.key()]++;
        }
    }
    QVector<QPair<double, QString>> list;
    for (auto it = sum.constBegin(); it != sum.constEnd(); ++it)
        list.append({ it.value() * in.value(it.key()), it.key() });   // a több meetingben közös előre
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    QStringList out;
    for (const auto& p : std::as_const(list)) {
        if (out.size() >= limit) break;
        out << d->display.value(p.second, p.second);
    }
    return out;
}

QStringList MeetingProfiles::sharedTerms(const QString& a, const QString& b, int limit) const
{
    d->computeStats();
    return d->shared(a, b, limit);
}

double MeetingProfiles::participantWeight(const QString& name) const
{
    d->computeStats();
    return d->personWeight.value(name, 1.0);
}

} // namespace tanara
