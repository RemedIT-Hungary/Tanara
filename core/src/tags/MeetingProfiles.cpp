#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagNames.h"
#include "tanara/Logging.h"
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
#include <QMutex>
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

// Szálbiztonság (lásd a fejlécet):
//  - A közös állapotot (sorok, kész profilok, a statisztika-pillanatkép) a `mutex` védi; a
//    zárat csak másolásra / beillesztésre tartjuk (a QHash implicit megosztott, a másolat
//    O(1)), lemez-I/O és számolás sosem fut alatta.
//  - A könyvtár-szintű statisztika megváltoztathatatlan pillanatkép (Stats): az olvasó
//    (bármelyik szálon) egy shared_ptr-t kap, és zár nélkül dolgozik rajta. Ha elavult (a
//    `gen` azóta nőtt), az olvasó újraszámolja; egyszerre egy számítás fut (`computeMutex`).
//    A fő szál nem vár egy másik szálon futó számításra: addig a korábbi pillanatképet kapja.
//  - Lemez-I/O (meeting.json, profile.json, átirat) a „tanara-profiles” szálon fut; a fő
//    szál csak az index-lekérdezést (MeetingStore::loadAll / folderOf, SQLite) végzi, mert az
//    adatbázis-kapcsolat a fő szálhoz kötött.
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
        bool operator==(const Row&) const = default;
    };
    struct Built {
        FileStamp stamp;
        RawTerms  raw;
    };
    // A könyvtár-szintű statisztika egy adott állapotra (gen) — megváltoztathatatlan.
    struct Stats {
        quint64 gen = 0;
        QHash<QString, Row> rows;
        QHash<QString, QString> canon;                 // nyers tő → összevont tő
        QHash<QString, QString> display;               // összevont tő → megjelenített alak
        QHash<QString, QHash<QString, double>> vec;    // meetingId → (tő → súly), ≤ 60
        QHash<QString, double> norm;
        QHash<QString, double> personWeight;           // név → ritkasági súly
    };
    enum class RowsState { NotLoaded, Loading, Loaded };

    // ---- közös állapot (mutex) ----
    mutable QMutex mutex;
    RowsState rowsState = RowsState::NotLoaded;
    QHash<QString, Row> rows;
    QHash<QString, Built> built;
    QSet<QString> removedWhileLoading;
    quint64 gen = 1;                                   // minden változás növeli
    mutable std::shared_ptr<const Stats> stats;        // a legutóbbi pillanatkép
    mutable QMutex computeMutex;

    // ---- csak a tulajdonos szálon ----
    int pendingSyncs = 0;
    std::atomic<bool> syncQueued{false};
    // Háttér-mód (az első ensureBuilt után): a fő szál sosem tölt be sorokat és sosem számol
    // statisztikát, az elavult pillanatképet a háttérszál frissíti.
    std::atomic<bool> background{false};
    mutable std::atomic<bool> recomputeQueued{false};

    static Row rowOf(const Meeting& m) {
        return Row{ m.title, meetingnotes::titleWords(m.title), MeetingLibrary::participantsOf(m),
                    m.folder, m.hasTranscript };
    }
    static Row rowFromIndex(const Meeting& idx) {
        Meeting m = MeetingStore::readMeetingFolder(idx.folder);
        if (m.id.isEmpty()) m = idx;
        if (m.folder.isEmpty()) m.folder = idx.folder;
        return rowOf(m);
    }
    bool onOwnerThread() const { return QThread::currentThread() == q->thread(); }

    // Régi, szinkron út: ha még senki nem kérte a háttér-építést (ensureBuilt), a tulajdonos
    // szálon az első olvasás tölti be a sorokat. Az alkalmazás az indításkor ensureBuilt-et
    // hív, így ott ez nem fut; a tesztekben és a CLI-ben kényelmes.
    void ensureRowsSync() const {
        if (!store || background || !onOwnerThread()) return;
        {
            QMutexLocker lock(&mutex);
            if (rowsState != RowsState::NotLoaded) return;
        }
        PerfScope perf("MeetingProfiles: sorok szinkron betöltése (fő szál)", 0);
        QHash<QString, Row> fresh;
        for (const Meeting& idx : store->loadAll()) fresh.insert(idx.id, rowFromIndex(idx));
        auto* self = const_cast<Impl*>(this);
        QMutexLocker lock(&self->mutex);
        if (self->rowsState != RowsState::NotLoaded) return;
        self->rows = fresh;
        self->rowsState = RowsState::Loaded;
        ++self->gen;
    }

    // Egy meeting sorának frissítése a háttérszálon (meeting.json); a fő szál csak a mappát
    // keresi ki (ha a sor még nem ismert).
    void reloadRow(const QString& id) {
        QString folder;
        {
            QMutexLocker lock(&mutex);
            if (rowsState == RowsState::NotLoaded) return;
            folder = rows.value(id).folder;
        }
        if (folder.isEmpty() && store) folder = store->folderOf(id);
        QMetaObject::invokeMethod(worker, [this, id, folder]() {
            if (worker->stop) return;
            const Meeting m = MeetingStore::readMeetingFolder(folder);
            QMutexLocker lock(&mutex);
            if (m.id.isEmpty()) {
                if (!rows.contains(id) && !built.contains(id)) return;
                rows.remove(id);
                built.remove(id);
            } else {
                Meeting mm = m;
                if (mm.folder.isEmpty()) mm.folder = folder;
                const Row row = rowOf(mm);
                const auto old = rows.constFind(id);
                if (old != rows.constEnd() && *old == row) return;   // nincs érdemi változás
                rows.insert(id, row);
            }
            ++gen;
        }, Qt::QueuedConnection);
    }

    void removeMeeting(const QString& id) {
        QMutexLocker lock(&mutex);
        rows.remove(id);
        built.remove(id);
        if (rowsState == RowsState::Loading) removedWhileLoading.insert(id);
        ++gen;
    }

    // A háttérszál egy köre: (ha kell) a sorok betöltése, az elavult profilok építése, végül
    // a statisztika előmelegítése.
    void syncJob(bool loadRows, const QVector<Meeting>& index) {
        syncQueued = false;
        if (worker->stop) return;
        if (loadRows) {
            PerfScope perf("MeetingProfiles: sorok betöltése (háttérszál)", 0);
            QHash<QString, Row> fresh;
            for (const Meeting& idx : index) {
                if (worker->stop) return;
                fresh.insert(idx.id, rowFromIndex(idx));
            }
            QMutexLocker lock(&mutex);
            for (const QString& id : std::as_const(removedWhileLoading)) fresh.remove(id);
            removedWhileLoading.clear();
            // Közben beérkezett sor-frissítések (reloadRow) a betöltés után futnak — sorrendben.
            rows = fresh;
            rowsState = RowsState::Loaded;
            ++gen;
        }
        QHash<QString, Row> rowsCopy;
        QHash<QString, FileStamp> stamps;
        {
            QMutexLocker lock(&mutex);
            rowsCopy = rows;
            for (auto it = built.constBegin(); it != built.constEnd(); ++it) stamps.insert(it.key(), it->stamp);
        }
        for (auto it = rowsCopy.constBegin(); it != rowsCopy.constEnd(); ++it) {
            if (worker->stop) return;
            const QString id = it.key();
            if (!it->hasTranscript || it->folder.isEmpty()) continue;
            const FileStamp stamp = FileStamp::of(segmentsPath(it->folder));
            if (stamp.mtimeMs < 0) continue;
            const auto b = stamps.constFind(id);
            if (b != stamps.constEnd() && *b == stamp) continue;
            RawTerms raw;
            if (!readProfileFile(it->folder, stamp, &raw)) {
                raw = countTerms(it->folder);
                writeProfileFile(it->folder, stamp, raw);
            }
            bool inserted = false;
            {
                QMutexLocker lock(&mutex);
                if (rows.contains(id)) {
                    built.insert(id, Built{ stamp, raw });
                    ++gen;
                    inserted = true;
                }
            }
            if (inserted)
                QMetaObject::invokeMethod(q, [this, id]() { emit q->profileReady(id); }, Qt::QueuedConnection);
        }
        if (worker->stop) return;
        (void)snapshot();   // előmelegítés: a fő szál olvasói már kész statisztikát kapnak
    }

    // A statisztika aktuális pillanatképe (sosem null).
    std::shared_ptr<const Stats> snapshot() const {
        {
            QMutexLocker lock(&mutex);
            if (stats && stats->gen == gen) return stats;
        }
        if (onOwnerThread() && background) {
            // A fő szál nem számol: a háttérszál frissít, addig a korábbi pillanatkép él.
            if (!recomputeQueued.exchange(true))
                QMetaObject::invokeMethod(worker, [this]() {
                    recomputeQueued = false;
                    if (!worker->stop) (void)snapshot();
                }, Qt::QueuedConnection);
            QMutexLocker lock(&mutex);
            return stats ? stats : std::make_shared<const Stats>();
        }
        if (onOwnerThread()) {
            if (!computeMutex.tryLock()) {
                // Másik szál épp számol: a fő szál nem vár, a korábbi pillanatkép is megfelel.
                QMutexLocker lock(&mutex);
                return stats ? stats : std::make_shared<const Stats>();
            }
        } else {
            computeMutex.lock();
        }
        QHash<QString, Built> builtCopy;
        auto s = std::make_shared<Stats>();
        {
            QMutexLocker lock(&mutex);
            if (stats && stats->gen == gen) { computeMutex.unlock(); return stats; }
            s->gen = gen;
            s->rows = rows;
            builtCopy = built;
        }
        {
            PerfScope perf(onOwnerThread() ? "MeetingProfiles: statisztika (fő szál)"
                                           : "MeetingProfiles: statisztika (háttérszál)", 0);
            computeStats(*s, builtCopy);
        }
        {
            QMutexLocker lock(&mutex);
            if (!stats || stats->gen < s->gen) stats = s;
        }
        computeMutex.unlock();
        return s;
    }

    static void computeStats(Stats& s, const QHash<QString, Built>& built) {
        const QHash<QString, Row>& rows = s.rows;

        // Résztvevők ritkasága: aki a megbeszélések > 60 %-án ott van, ~0.
        const int nAll = int(rows.size());
        QHash<QString, int> pdf;
        for (const Row& r : rows)
            for (const QString& p : r.participants) pdf[p]++;
        for (auto it = pdf.constBegin(); it != pdf.constEnd(); ++it) {
            const double share = nAll > 0 ? double(it.value()) / nAll : 1.0;
            s.personWeight.insert(it.key(), std::max(0.0, 1.0 - share / 0.6));
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
            const QString& w = it.key();
            if (it.value() > rareDf || w.size() < 5) continue;
            buckets[w].append(w);
            for (int i = 0; i < w.size(); ++i) buckets[w.left(i) + w.mid(i + 1)].append(w);
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
        const QStringList members = uf.parent.keys();
        for (const QString& key : members) {
            const QString root = uf.find(key);
            for (const QString& member : { key, root }) {
                const QString cur = groupBest.value(root);
                if (cur.isEmpty() || rawTotal.value(member) > rawTotal.value(cur)
                    || (rawTotal.value(member) == rawTotal.value(cur) && member.size() > cur.size()))
                    groupBest[root] = member;
            }
        }
        for (auto it = rawDf.constBegin(); it != rawDf.constEnd(); ++it) {
            const QString w = it.key();
            s.canon.insert(w, uf.parent.contains(w) ? groupBest.value(uf.find(w)) : w);
        }

        // Megjelenített alak: az összevont csoport leggyakoribb eredeti írásmódja.
        QHash<QString, QHash<QString, int>> canonForms;
        for (auto it = surfaces.constBegin(); it != surfaces.constEnd(); ++it)
            for (auto f = it->constBegin(); f != it->constEnd(); ++f)
                canonForms[s.canon.value(it.key())][f.key()] += f.value();
        for (auto it = canonForms.constBegin(); it != canonForms.constEnd(); ++it) {
            QString best; int bestN = -1;
            for (auto f = it->constBegin(); f != it->constEnd(); ++f)
                if (f.value() > bestN || (f.value() == bestN && f.key() < best)) { best = f.key(); bestN = f.value(); }
            s.display.insert(it.key(), best);
        }

        // Összevont df, majd meetingenként tf × log(N / df), a legerősebb 60.
        QHash<QString, QHash<QString, int>> ctf;
        QHash<QString, int> df;
        for (auto it = built.constBegin(); it != built.constEnd(); ++it) {
            if (!rows.contains(it.key()) || it->raw.tf.isEmpty()) continue;
            QHash<QString, int>& m = ctf[it.key()];
            for (auto t = it->raw.tf.constBegin(); t != it->raw.tf.constEnd(); ++t)
                m[s.canon.value(t.key(), t.key())] += t.value();
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
            s.vec.insert(it.key(), v);
            s.norm.insert(it.key(), std::sqrt(sq));
        }
    }

    static double participantScore(const Stats& s, const QStringList& a, const QStringList& b, QStringList* shared) {
        double inter = 0, uni = 0;
        QSet<QString> all(a.cbegin(), a.cend());
        for (const QString& p : b) all.insert(p);
        QVector<QPair<double, QString>> common;
        for (const QString& p : std::as_const(all)) {
            const double w = s.personWeight.value(p, 1.0);
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

    static QStringList shared(const Stats& s, const QString& a, const QString& b, int limit) {
        const QHash<QString, double> va = s.vec.value(a), vb = s.vec.value(b);
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
            out << s.display.value(c.second, c.second);
        }
        return out;
    }

    static QVector<SimilarHit> rank(const Stats& s, const QString& selfId, const QStringList& participants,
                                    const QStringList& titleWords, int limit) {
        QVector<SimilarHit> hits;
        const bool hasVec = s.vec.contains(selfId);
        const QHash<QString, double> selfVec = s.vec.value(selfId);
        const double selfNorm = s.norm.value(selfId);
        for (auto it = s.rows.constBegin(); it != s.rows.constEnd(); ++it) {
            if (it.key() == selfId) continue;
            QStringList sharedPeople;
            const double p = participantScore(s, participants, it->participants, &sharedPeople);
            const double t = hasVec && s.vec.contains(it.key())
                ? cosine(selfVec, selfNorm, s.vec.value(it.key()), s.norm.value(it.key())) : 0.0;
            const double ti = meetingnotes::titleSimilarity(titleWords, it->titleWords);
            const double score = kWeightParticipants * p + kWeightTerms * t + kWeightTitle * ti;
            if (score <= 0.0) continue;
            SimilarHit h;
            h.meetingId = it.key();
            h.score = score;
            if (p > 0 && !sharedPeople.isEmpty())
                h.reasons.append({ ReasonKind::Participant, sharedPeople.mid(0, 3) });
            if (t > 0) {
                const QStringList terms = shared(s, selfId, it.key(), 3);
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
        connect(store, &MeetingStore::meetingRemoved, this, [this](const QString& id) { d->removeMeeting(id); });
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
    d->background = true;
    // Egy várakozó kör elég: ami addig változik, azt az is látja.
    if (d->syncQueued.exchange(true)) return;
    bool loadRows = false;
    QVector<Meeting> index;
    {
        QMutexLocker lock(&d->mutex);
        if (d->rowsState == Impl::RowsState::NotLoaded && d->store) {
            d->rowsState = Impl::RowsState::Loading;
            loadRows = true;
        }
    }
    if (loadRows) {
        PerfScope perf("MeetingProfiles::ensureBuilt: index (fő szál)", 5);
        index = d->store->loadAll();   // csak az index (SQLite), fájl-olvasás nélkül
    }
    ++d->pendingSyncs;
    QMetaObject::invokeMethod(d->worker, [this, loadRows, index]() {
        d->syncJob(loadRows, index);
        QMetaObject::invokeMethod(this, [this]() {
            if (--d->pendingSyncs == 0) emit idle();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

bool MeetingProfiles::isIdle() const { return d->pendingSyncs == 0; }

bool MeetingProfiles::isBuilt(const QString& meetingId) const
{
    QMutexLocker lock(&d->mutex);
    return d->built.contains(meetingId);
}

void MeetingProfiles::invalidate(const QString& meetingId)
{
    {
        QMutexLocker lock(&d->mutex);
        d->built.remove(meetingId);
        ++d->gen;
    }
    d->reloadRow(meetingId);
}

QHash<QString, QString> MeetingProfiles::folders() const
{
    d->ensureRowsSync();
    QHash<QString, Impl::Row> rows;
    {
        QMutexLocker lock(&d->mutex);
        rows = d->rows;
    }
    QHash<QString, QString> out;
    out.reserve(rows.size());
    for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) out.insert(it.key(), it->folder);
    return out;
}

QVector<SimilarHit> MeetingProfiles::similar(const QString& meetingId, int limit) const
{
    d->ensureRowsSync();
    const auto s = d->snapshot();
    const auto self = s->rows.constFind(meetingId);
    if (self == s->rows.constEnd()) return {};
    return Impl::rank(*s, meetingId, self->participants, self->titleWords, limit);
}

QVector<SimilarHit> MeetingProfiles::similarToDraft(const QString& title, const QStringList& participants,
                                                    const QString& excludeId, int limit) const
{
    d->ensureRowsSync();
    const QString self = excludeId.isEmpty() ? QStringLiteral("\x01draft") : excludeId;
    return Impl::rank(*d->snapshot(), self, participants, meetingnotes::titleWords(title), limit);
}

QStringList MeetingProfiles::termsOf(const QString& meetingId, int limit) const
{
    return topTerms({ meetingId }, limit);
}

QStringList MeetingProfiles::topTerms(const QStringList& meetingIds, int limit) const
{
    d->ensureRowsSync();
    const auto s = d->snapshot();
    QHash<QString, double> sum;
    QHash<QString, int> in;
    for (const QString& id : meetingIds) {
        const QHash<QString, double> v = s->vec.value(id);
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
        out << s->display.value(p.second, p.second);
    }
    return out;
}

QStringList MeetingProfiles::sharedTerms(const QString& a, const QString& b, int limit) const
{
    d->ensureRowsSync();
    return Impl::shared(*d->snapshot(), a, b, limit);
}

double MeetingProfiles::participantWeight(const QString& name) const
{
    d->ensureRowsSync();
    return d->snapshot()->personWeight.value(name, 1.0);
}

} // namespace tanara
