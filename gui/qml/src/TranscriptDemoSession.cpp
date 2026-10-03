#include "TranscriptDemoSession.h"

#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

// Egy forgatókönyv-sor: ki mondja (beszélő-index), kinek a HANGJÁN szól (voice; -1 = a
// sajátján), mikor, meddig, mit.
struct Line {
    qint64 startMs;
    qint64 durMs;
    int speaker;
    int voice;
    QString text;
};

struct Cast {
    QString raw;        // nyers diarizációs címke
    QString person;     // üres = névtelen
    bool voiceprint;
    double weight;      // a töltelék-sorok eloszlásához
};

// A hamis embedder: a szelet közepe alapján adja a hang egységvektorát (beszélőnként
// ortogonális), ahogy a core unit-tesztjei is teszik.
class DemoEmbedder : public IUtteranceEmbedder {
public:
    explicit DemoEmbedder(std::shared_ptr<const QVector<Line>> lines, int dim)
        : m_lines(std::move(lines)), m_dim(dim) {}
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        const qint64 mid = (startMs + endMs) / 2;
        const auto it = std::upper_bound(m_lines->cbegin(), m_lines->cend(), mid,
                                         [](qint64 t, const Line& l) { return t < l.startMs; });
        if (it == m_lines->cbegin()) return {};
        const Line& l = *(it - 1);
        QVector<float> v(m_dim, 0.0f);
        v[(l.voice >= 0 ? l.voice : l.speaker) % m_dim] = 1.0f;
        return v;
    }
private:
    std::shared_ptr<const QVector<Line>> m_lines;
    int m_dim;
};

qint64 ts(int min, int sec) { return (qint64(min) * 60 + sec) * 1000; }

// Töltelék-mondatok (kitalált, általános termékcsapat-megbeszélés).
const QStringList& fillerPool()
{
    static const QStringList pool{
        QStringLiteral("A következő kiadásba ez még belefér, ha a tesztelés időben végez."),
        QStringLiteral("Szerintem előbb nézzük meg a számokat, utána döntsünk."),
        QStringLiteral("Rendben, ezt felírom a teendők közé."),
        QStringLiteral("A súgóoldalak látogatottsága a múlt héten is nőtt, főleg a beállításoknál."),
        QStringLiteral("Ezt az ügyfelek oldaláról is megerősítették, két külön megkeresésben is szóba került."),
        QStringLiteral("Akkor a határidő marad péntek, és csütörtökön még egyszer ránézünk."),
        QStringLiteral("Nálunk a legtöbb kérdés a számlázás körül jön, nem a belépésnél."),
        QStringLiteral("Ha kell, szívesen megmutatom a bontást a megbeszélés végén."),
        QStringLiteral("Egyetértek, de a dokumentációt is frissíteni kell hozzá, különben megint ugyanott akadnak el."),
        QStringLiteral("Ezt a pontot hagyjuk a jövő heti tervezésre, ott több időnk lesz rá."),
        QStringLiteral("A tesztkörnyezet tegnap óta stabil, a két nyitott hibajegy közül az egyik csak kozmetikai."),
        QStringLiteral("Jó, akkor én viszem a javítást, és szólok, ha kész."),
        QStringLiteral("A partnereknek érdemes előre jelezni, hogy változik a felület."),
        QStringLiteral("Még egy kérdés: ez a régi ügyfeleket is érinti, vagy csak az újakat?"),
        QStringLiteral("Csak az újakat, a régieknél marad a mostani folyamat a negyedév végéig."),
        QStringLiteral("Értem, köszönöm."),
        QStringLiteral("A keresés javítása hozta a legnagyobb változást: a találatok kétharmada már az első oldalon megvan, "
                       "és a visszajelzések szerint a kollégák is ritkábban kérdeznek rá ugyanarra."),
        QStringLiteral("Ezt döntésként rögzítsük: a bevezetés két lépésben megy, először a belső csapatnak."),
        QStringLiteral("Igen, így jó lesz."),
        QStringLiteral("A költségvetésnél még visszatérünk rá, addig nem ígérnék pontos dátumot."),
    };
    return pool;
}

QString longMonologue()
{
    // Nagyon hosszú megszólalás (~280 szó) a „hosszú monológ" állapothoz.
    QStringList parts;
    parts << QStringLiteral("Köszönöm, hogy ilyen sokan eljöttetek. Mielőtt belevágunk, szeretném egyben végigmondani, "
                            "hol tartunk, mert az elmúlt hetekben sok minden változott, és nem mindenki volt ott minden "
                            "egyeztetésen.");
    const QStringList& pool = fillerPool();
    for (int i = 0; i < 22; ++i) parts << pool[(i * 7 + 3) % pool.size()];
    parts << QStringLiteral("Ennyit szerettem volna előrebocsátani; most pedig menjünk végig a részleteken, és a végén "
                            "marad idő a kérdésekre is.");
    return parts.join(QLatin1Char(' '));
}

struct Scenario {
    QVector<Cast> cast;
    QVector<Line> lines;
    int seedLine = -1;      // a javaslat-állapot „kézzel átrakandó" sora
    int seedTarget = -1;
    qint64 durationMs = 0;
    bool transcript = true;
};

// Töltelék: a megadott időtől a meeting végéig, súlyozott beszélő-sorrenddel. `stray`:
// (beszélő, hang) párok, amelyek időnként „rossz hangon" szólalnak meg (bizonytalan sorok).
void fill(Scenario& sc, qint64 fromMs, qint64 untilMs, quint32 seed,
          const QVector<QPair<int, int>>& strayVoices, int strayEvery)
{
    quint64 x = seed;
    auto rnd = [&x] { x = (x * 16807) % 2147483647ULL; return double(x) / 2147483647.0; };
    double total = 0;
    for (const Cast& c : std::as_const(sc.cast)) total += c.weight;
    const QStringList& pool = fillerPool();
    qint64 t = fromMs;
    int last = -1, n = 0;
    while (t < untilMs - 12000) {
        int k = 0;
        do {
            double r = rnd() * total;
            for (k = 0; k < sc.cast.size() - 1; ++k) {
                r -= sc.cast[k].weight;
                if (r < 0) break;
            }
        } while (k == last && sc.cast.size() > 1);
        last = k;
        // Egy fordulóban 1–3 megszólalás (bekezdés) ugyanattól a beszélőtől.
        const int paragraphs = 1 + int(rnd() * 2.4);
        for (int p = 0; p < paragraphs && t < untilMs - 8000; ++p) {
            const QString& text = pool[int(rnd() * pool.size()) % pool.size()];
            Line l;
            l.startMs = t;
            l.durMs = qMax<qint64>(1700, qint64(text.size()) * 62);
            l.speaker = k;
            l.voice = -1;
            l.text = text;
            ++n;
            if (strayEvery > 0 && n % strayEvery == 0) {
                for (const auto& sv : strayVoices)
                    if (sv.first == k) { l.voice = sv.second; break; }
            }
            sc.lines.append(l);
            t += l.durMs + 400 + qint64(rnd() * 2600);
        }
    }
    sc.durationMs = untilMs;
}

Scenario fourSpeakers(bool longFirst)
{
    Scenario sc;
    sc.cast = {
        {QStringLiteral("Beszélő 1"), QStringLiteral("Kovács Lilla"), true, 1.2},
        {QStringLiteral("Beszélő 2"), QStringLiteral("Fehér Ádám"), true, 3.4},
        {QStringLiteral("Beszélő 3"), QStringLiteral("Varga Nóra"), true, 2.8},
        {QStringLiteral("Távoli 1"), QString(), false, 1.7},
    };
    const QString first = longFirst ? longMonologue()
        : QStringLiteral("Köszönöm, hogy ilyen sokan eljöttetek. Röviden végigmegyek a harmadik negyedéven, utána "
                         "szívesen válaszolok a kérdésekre. Az első és legfontosabb: a támogatási jegyek száma "
                         "harmadával csökkent, miközben az aktív felhasználók száma nagyjából tizenkét százalékkal "
                         "nőtt. Ez azt jelenti, hogy egy felhasználóra most lényegesen kevesebb megkeresés jut, mint "
                         "egy évvel ezelőtt, és ezt nem a csapat leépítése okozta, hanem az, hogy a leggyakoribb "
                         "kérdéseket végre a termékben válaszoljuk meg.");
    sc.lines = {
        {ts(0, 4), 36000, 0, -1, first},
        {ts(0, 41), 30000, 0, -1,
         QStringLiteral("A második pont az új súgóoldalak. Júliusban élesítettük őket, és az első hat hétben a "
                        "keresések kétharmada már ott ért véget, nem a jegykezelőben. Ami külön érdekes: a legtöbb "
                        "látogatás a beállítási oldalakról érkezik, tehát pont ott, ahol korábban a legtöbben "
                        "elakadtak. A következő negyedévben ezt a mintát szeretnénk a számlázásra is átvinni.")},
        {ts(1, 12), 3600, 1, -1,
         QStringLiteral("Bocs, hogy közbeszólok, ez a szám az összes ügyfélre vonatkozik, vagy csak az újakra?")},
        {ts(1, 16), 1600, 0, -1, QStringLiteral("Az összesre.")},
        {ts(1, 18), 9000, 0, -1,
         QStringLiteral("Az újaknál egyébként még jobb az arány, de ott kicsi a minta, ezért nem tettem fel a diára. "
                        "Ha érdekel, a végén megmutatom a bontást.")},
        {ts(1, 39), 1800, 3, 2, QStringLiteral("Mhm.")},
        // Nyersen „Távoli 1", de hangra Fehér Ádám: a javaslat-állapot ezt rakja át.
        {ts(1, 42), 3800, 3, 1, QStringLiteral("Ez egyezik azzal, amit a támogatás oldaláról látunk.")},
        {ts(2, 2), 12000, 2, -1,
         QStringLiteral("Egy kérdés a számlázáshoz: ott nem az a gond, hogy a súgó nem elég, hanem hogy maga a "
                        "folyamat bonyolult? Szerintem ott a terméket kellene egyszerűsíteni, nem a leírást bővíteni.")},
        {ts(2, 15), 3000, 0, -1,
         QStringLiteral("Jó kérdés, erre a végén visszatérek, mert van róla egy külön diám.")},
        {ts(2, 19), 4200, 3, -1,
         QStringLiteral("Elnézést, megismételnéd az utolsó mondatot? Itt akadozott a hang.")},
        {ts(2, 24), 21000, 0, -1,
         QStringLiteral("Persze. Azt mondtam, hogy a számlázásra is átvinnénk a súgót, de Nórának igaza van abban, "
                        "hogy ott a folyamat maga is túl hosszú. A terv ezért két lépésből áll: először "
                        "összegyűjtjük, hol akadnak el a legtöbben, aztán eldöntjük, mit javítunk a termékben és "
                        "mit a leírásban.")},
    };
    if (longFirst) {
        // A hosszú monológ kitolja a többi sort.
        const qint64 shift = 95000;
        sc.lines[0].durMs += shift;
        for (int i = 1; i < sc.lines.size(); ++i) sc.lines[i].startMs += shift;
    }
    sc.seedLine = 6;
    sc.seedTarget = 1;
    const qint64 from = sc.lines.last().startMs + sc.lines.last().durMs + 1500;
    // A „Távoli 1" címke időnként Fehér Ádám hangján szól (ezekre jön a javaslat); Varga Nóra
    // egy-egy sora pedig Kovács Lilla hangján (sima bizonytalan sor).
    fill(sc, from, ts(30, 34), 7, {{3, 1}, {2, 0}}, 23);
    return sc;
}

Scenario twoSpeakers()
{
    Scenario sc;
    sc.cast = {
        {QStringLiteral("Beszélő 1"), QStringLiteral("Kovács Lilla"), true, 1.0},
        {QStringLiteral("Beszélő 2"), QStringLiteral("Fehér Ádám"), true, 1.1},
    };
    sc.lines = {
        {ts(0, 3), 4200, 0, -1, QStringLiteral("Akkor kezdjük az elején: mióta használjátok a jelenlegi jegykezelőt?")},
        {ts(0, 9), 7000, 1, -1,
         QStringLiteral("Nagyjából három éve. Előtte e-mailből dolgoztunk, ami tíz ember fölött már követhetetlen volt.")},
        {ts(0, 21), 5000, 0, -1,
         QStringLiteral("Az első évben főleg a címkézéssel küzdöttünk, mindenki máshogy használta.")},
        {ts(0, 27), 4600, 0, -1, QStringLiteral("Aztán bevezettünk egy közös listát, és onnantól sokkal jobb lett.")},
        {ts(0, 33), 3400, 0, -1, QStringLiteral("Ma már az új kollégák is egy hét alatt beletanulnak.")},
        {ts(0, 38), 3000, 0, -1, QStringLiteral("És mi az, ami most a legjobban hiányzik belőle?")},
        {ts(0, 42), 2200, 1, 0, QStringLiteral("Hm, talán a jobb keresés.")},
        {ts(0, 45), 6500, 1, -1,
         QStringLiteral("Meg az, hogy a hívásokból ne nekünk kelljen kézzel jegyet írni. Ezért is néztük a Tanarát.")},
    };
    fill(sc, ts(0, 54), ts(18, 20), 23, {}, 0);
    return sc;
}

Scenario manySpeakers()
{
    Scenario sc;
    sc.cast = {
        {QStringLiteral("Beszélő 1"), QStringLiteral("Kovács Lilla"), true, 3.0},
        {QStringLiteral("Beszélő 2"), QStringLiteral("Fehér Ádám"), true, 4.6},
        {QStringLiteral("Beszélő 3"), QStringLiteral("Varga Nóra"), true, 1.6},
        {QStringLiteral("Beszélő 4"), QStringLiteral("Tóth Bence"), true, 1.2},
        {QStringLiteral("Beszélő 5"), QStringLiteral("Molnár Eszter"), true, 2.2},
        {QStringLiteral("Beszélő 6"), QStringLiteral("Szabó Áron"), false, 1.5},
        {QStringLiteral("Beszélő 7"), QStringLiteral("Németh Dávid"), true, 0.22},
        {QStringLiteral("Beszélő 8"), QStringLiteral("Horváth Gergő"), false, 0.2},
        {QStringLiteral("Távoli 1"), QString(), false, 0.18},
        {QStringLiteral("Távoli 2"), QString(), false, 0.15},
        {QStringLiteral("Távoli 3"), QString(), false, 0.12},
    };
    sc.lines = {
        {ts(0, 2), 5200, 3, -1, QStringLiteral("A második negyedévben a támogatási jegyek száma harmadával csökkent.")},
        {ts(0, 9), 5000, 4, -1,
         QStringLiteral("Ez az új súgóoldalaknak köszönhető, vagy annak, hogy kevesebb az ügyfél?")},
        {ts(0, 15), 2800, 3, -1, QStringLiteral("Mindkettőnek, de inkább a súgónak.")},
        {ts(0, 19), 2000, 9, 2, QStringLiteral("Nálunk is ezt látjuk.")},
        {ts(0, 22), 3600, 0, -1, QStringLiteral("Akkor a súgót a többi termékre is kiterjesztjük.")},
        {ts(0, 27), 3800, 5, -1, QStringLiteral("Ehhez kellene még egy fő a dokumentációra.")},
        {ts(0, 32), 2600, 6, -1, QStringLiteral("Ezt a költségvetésnél nézzük meg.")},
        {ts(0, 36), 1900, 2, 4, QStringLiteral("Rendben, felírom.")},
        // Mindenki megszólal egyszer, hogy a 11 beszélő biztosan szerepeljen.
        {ts(0, 40), 3000, 1, -1, QStringLiteral("Én a kiadás felől nézném: mi fér bele a mostani körbe?")},
        {ts(0, 44), 2600, 7, -1, QStringLiteral("A tesztkörnyezet felől nincs akadálya.")},
        {ts(0, 48), 2400, 8, -1, QStringLiteral("Halljátok? Most csatlakoztam.")},
        {ts(0, 52), 2400, 10, -1, QStringLiteral("Igen, jól hallunk, köszi.")},
    };
    fill(sc, ts(0, 57), ts(52, 10), 19, {{2, 0}, {5, 1}}, 29);
    return sc;
}

void writeJson(const QString& path, const QJsonDocument& doc)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(doc.toJson(QJsonDocument::Compact));
}

} // namespace

TranscriptDemoSession::TranscriptDemoSession(const QString& variant)
{
    Scenario sc;
    if (variant == QLatin1String("two")) sc = twoSpeakers();
    else if (variant == QLatin1String("many")) sc = manySpeakers();
    else sc = fourSpeakers(variant == QLatin1String("long"));
    if (variant == QLatin1String("none")) sc.transcript = false;

    m_store = std::make_unique<MeetingStore>(m_dir.filePath(QStringLiteral("rec")),
                                             m_dir.filePath(QStringLiteral("meta")));
    m_people = std::make_unique<PeopleStore>(m_dir.filePath(QStringLiteral("meta/people.json")));
    m_prints = std::make_unique<VoiceprintStore>(m_dir.filePath(QStringLiteral("meta/voiceprints.json")));

    Meeting m = m_store->createMeeting(QStringLiteral("Termékcsapat heti egyeztetés"));
    m.durationMs = sc.durationMs;

    if (sc.transcript) {
        QJsonArray segs;
        for (const Line& l : std::as_const(sc.lines)) {
            QJsonObject o;
            o[QStringLiteral("startMs")] = double(l.startMs);
            o[QStringLiteral("endMs")] = double(l.startMs + l.durMs);
            o[QStringLiteral("speaker")] = sc.cast[l.speaker].raw;
            o[QStringLiteral("text")] = l.text;
            segs.append(o);
        }
        writeJson(speakeredit::segmentsPath(m.folder), QJsonDocument(segs));
        m.hasTranscript = true;
    }

    // Nevek + hanglenyomatok (a sáv-fejléc pöttyéhez) + egy hang-azonosítási pontszám.
    const int dim = int(sc.cast.size());
    for (int k = 0; k < sc.cast.size(); ++k) {
        const Cast& c = sc.cast[k];
        if (c.person.isEmpty()) continue;
        m.speakerMap.insert(c.raw, c.person);
        m_people->add(c.person);
        if (c.voiceprint) {
            Voiceprint p;
            p.embedding = QVector<float>(dim, 0.0f);
            p.embedding[k] = 1.0f;
            p.sourceMeetingId = m.id;
            m_prints->addPrint(c.person, p);
        }
    }
    m_store->saveMeeting(m);
    if (sc.transcript && !sc.cast.isEmpty() && !sc.cast[0].person.isEmpty())
        speakeredit::recordIdentification(m.folder, sc.cast[0].raw, sc.cast[0].person, 0.82);

    m_editor = std::make_unique<SpeakerEditor>(m_store.get(), m_people.get(), m_prints.get(), m.id);
    if (variant != QLatin1String("novoice")) {
        const auto lines = std::make_shared<const QVector<Line>>(sc.lines);
        m_editor->setEmbedderFactory([lines, dim] { return std::make_unique<DemoEmbedder>(lines, dim); });
    }
    if (sc.transcript && sc.seedLine >= 0) {
        m_seedId = m_editor->utteranceAt(sc.seedLine).id;
        m_seedTarget = sc.cast[sc.seedTarget].raw;
    }
}

TranscriptDemoSession::~TranscriptDemoSession()
{
    m_editor.reset();   // a háttérszál bevárása, mielőtt a store-ok megszűnnek
}

QVector<PersonInfo> TranscriptDemoSession::people() const
{
    // Kitalált névlista a választókhoz; a meeting személyei a valódi lenyomat-állapotukkal.
    struct P { const char* name; bool vp; int meetings; };
    static const P extra[] = {
        {"Balogh Kata", true, 12}, {"Bálint Péter", false, 3}, {"Baranyi Zsófia", true, 7},
        {"Molnár Eszter", true, 9}, {"Kovács Lilla", true, 14}, {"Fehér Ádám", true, 11},
        {"Varga Nóra", true, 8}, {"Tóth Bence", true, 6}, {"Szabó Áron", false, 4},
        {"Németh Dávid", true, 5}, {"Horváth Gergő", false, 2}, {"Ördög Ödön", false, 1},
    };
    QVector<PersonInfo> out;
    QStringList seen;
    for (const P& p : extra) {
        PersonInfo i;
        i.name = QString::fromUtf8(p.name);
        i.voiceprintCount = m_prints->printCount(i.name);
        i.hasVoiceprint = p.vp || i.voiceprintCount > 0;
        if (i.hasVoiceprint && i.voiceprintCount == 0) i.voiceprintCount = 1;
        i.meetingCount = p.meetings;
        out.append(i);
        seen << i.name;
    }
    // A demóban felvett új személyek (people.json) is jelenjenek meg.
    for (const QString& name : m_people->names()) {
        if (seen.contains(name)) continue;
        PersonInfo i;
        i.name = name;
        i.voiceprintCount = m_prints->printCount(name);
        i.hasVoiceprint = i.voiceprintCount > 0;
        i.meetingCount = 1;
        out.append(i);
    }
    std::sort(out.begin(), out.end(), [](const PersonInfo& a, const PersonInfo& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

} // namespace tanara_qml
