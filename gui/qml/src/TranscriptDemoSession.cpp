#include "TranscriptDemoSession.h"

#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
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
#include <cmath>

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
    // noisy: a hang-vektor a beszélő tengelye mellett kis, determinisztikus zajt is kap (a v3
    // változat bizonyíték-százalékai így nem mind 100% / 0%).
    explicit DemoEmbedder(std::shared_ptr<const QVector<Line>> lines, int dim, bool noisy = false)
        : m_lines(std::move(lines)), m_dim(dim), m_noisy(noisy) {}
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        const qint64 mid = (startMs + endMs) / 2;
        const auto it = std::upper_bound(m_lines->cbegin(), m_lines->cend(), mid,
                                         [](qint64 t, const Line& l) { return t < l.startMs; });
        if (it == m_lines->cbegin()) return {};
        const Line& l = *(it - 1);
        QVector<float> v(m_dim, 0.0f);
        const int axis = (l.voice >= 0 ? l.voice : l.speaker) % m_dim;
        v[axis] = 1.0f;
        if (m_noisy) {
            quint64 x = quint64(l.startMs) * 2654435761ULL + 12345;
            double norm = 0.0;
            for (int k = 0; k < m_dim; ++k) {
                x = (x * 6364136223846793005ULL + 1442695040888963407ULL);
                const float r = float((x >> 33) % 1000) / 1000.0f;
                if (k == axis) v[k] = 0.80f + 0.20f * r;
                else v[k] = 0.22f * r;
                norm += double(v[k]) * v[k];
            }
            const float n = float(std::sqrt(norm));
            for (float& f : v) f /= n;
        }
        return v;
    }
private:
    std::shared_ptr<const QVector<Line>> m_lines;
    int m_dim;
    bool m_noisy;
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
    // v3 (a handoff-v3 nevei): sávok (mikrofon + hívás hangja) kitalált aktivitással, címkék,
    // egy cast-on kívüli hang (az „új személy"), zajos hang-vektorok.
    bool v3 = false;
    int voices = 0;     // a hang-dimenzió (0 = a cast mérete)
    int localVoice = 0; // ennek a hangnak a sorai a mikrofonon szólnak (a többi a híváson)
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

// A handoff-v3 meetingje: Kovács Lilla a saját mikrofonján, a többiek a hívás hangján. A
// forgatókönyv elején az E1–E4 sorai; utána töltelék, benne a csoportokhoz való „rossz" sorok:
// Lilla nevén Gábor hangja a hívásról (sáv-ellentmondás), Gábor nevén Árpád hangja (mag-eltérés),
// Gábor nevén egy cast-on kívüli hang (Nagy Péter, az E4 új személye), és rövid sorok.
Scenario v3Scenario()
{
    Scenario sc;
    sc.v3 = true;
    sc.voices = 7;          // 6 beszélő + Nagy Péter hangja (6)
    sc.localVoice = 0;
    sc.cast = {
        {QStringLiteral("Beszélő 1"), QStringLiteral("Kovács Lilla"), true, 1.2},
        {QStringLiteral("Beszélő 2"), QStringLiteral("Fehér Gábor"), true, 1.5},
        {QStringLiteral("Beszélő 3"), QStringLiteral("Varga Árpád"), true, 1.1},
        {QStringLiteral("Beszélő 4"), QStringLiteral("Molnár Eszter"), true, 0.9},
        {QStringLiteral("Beszélő 5"), QStringLiteral("Tóth Bence"), false, 0.6},
        {QStringLiteral("Távoli 1"), QString(), false, 0.35},
    };
    sc.lines = {
        {ts(0, 4), 6200, 0, -1,
         QStringLiteral("Igen, és ide tartozik még, hogy a számlázási rész lesz a következő; ott a legtöbb a visszakérdezés.")},
        {ts(0, 11), 1000, 1, -1, QStringLiteral("Pontosan.")},
        // Lilla nevén, de Gábor hangja a hívásról: „bizonytalan · sáv".
        {ts(0, 13), 7400, 0, 1,
         QStringLiteral("Igen, ezt mi is láttuk a partnerportálon, főleg a Nordvik-ügyfeleknél, ott a jegyek fele számlázási volt.")},
        // Árpád nevén, Eszter hangja: a „markers" állapot kézzel átteszi (javítva). (Árpád nyers
        // címkéje így Eszteré előtt jelenik meg: a színek sorrendje a handoffé.)
        {ts(0, 22), 6600, 2, 3,
         QStringLiteral("A mi oldalunkon ez a negyedik negyedév elejére kellene, különben a megújításnál újra elő fog jönni.")},
        {ts(0, 30), 5200, 1, -1,
         QStringLiteral("Ezt a részt én vállalom, de kellene hozzá két hét a dokumentációs csapattól.")},
        {ts(0, 36), 900, 3, -1, QStringLiteral("Értem.")},
        // Gábor nevén, de Nagy Péter hangja (E4: ez a három kerül az új személyhez).
        {ts(0, 38), 3200, 1, 6,
         QStringLiteral("És mikorra várható a számlázási súgó? Az ügyfeleink már kérdezik.")},
        {ts(0, 42), 4400, 1, 6,
         QStringLiteral("Én a pénzügy felől jövök: nálunk a számlázási kérdések fele a díjbekérőkről szól, nem magáról a számláról.")},
        {ts(0, 47), 3000, 1, 6, QStringLiteral("Ha a súgó ezt külön kezelné, szerintem a maradék is eltűnne.")},
    };
    const QStringList shortPool{QStringLiteral("Igen."), QStringLiteral("Mhm."), QStringLiteral("Értem."),
                                QStringLiteral("Jó."), QStringLiteral("Aha."), QStringLiteral("Pontosan.")};
    quint64 x = 41;
    auto rnd = [&x] { x = (x * 16807) % 2147483647ULL; return double(x) / 2147483647.0; };
    double total = 0;
    for (const Cast& c : std::as_const(sc.cast)) total += c.weight;
    const QStringList& pool = fillerPool();
    QVector<int> perSpeaker(sc.cast.size(), 0);
    qint64 t = ts(1, 0);
    const qint64 until = ts(44, 30);
    int last = -1, n = 0;
    while (t < until - 12000) {
        int k = 0;
        do {
            double r = rnd() * total;
            for (k = 0; k < sc.cast.size() - 1; ++k) {
                r -= sc.cast[k].weight;
                if (r < 0) break;
            }
        } while (k == last);
        last = k;
        const int paragraphs = 1 + int(rnd() * 2.2);
        for (int p = 0; p < paragraphs && t < until - 8000; ++p) {
            Line l;
            l.startMs = t;
            l.speaker = k;
            l.voice = -1;
            ++n;
            const int own = ++perSpeaker[k];
            if (n % 7 == 0) {
                l.text = shortPool[n % shortPool.size()];
                l.durMs = 700 + qint64(rnd() * 600);
            } else {
                l.text = pool[int(rnd() * pool.size()) % pool.size()];
                l.durMs = qMax<qint64>(2200, qint64(l.text.size()) * 62);
                if (k == 0 && own % 4 == 0) l.voice = 1;            // Lilla nevén Gábor (híváson)
                else if (k == 1 && own % 5 == 0) l.voice = 2;       // Gábor nevén Árpád
                else if (k == 1 && own % 11 == 0) l.voice = 6;      // Gábor nevén Nagy Péter
            }
            sc.lines.append(l);
            t += l.durMs + 400 + qint64(rnd() * 2400);
        }
    }
    sc.durationMs = until;
    return sc;
}

// A kitalált sáv-aktivitás: a sor ideje alatt a mikrofon VAGY a hívás-sáv szól (a hang gazdája
// szerint), a másik csendes.
MeetingActivity demoActivity(const Scenario& sc)
{
    constexpr int frameMs = 50;
    const int frames = int(sc.durationMs / frameMs) + 1;
    TrackActivity mic, loop;
    mic.trackId = QStringLiteral("mic");
    mic.kind = TrackKind::Mic;
    loop.trackId = QStringLiteral("loopback");
    loop.kind = TrackKind::Loopback;
    for (TrackActivity* a : {&mic, &loop}) {
        a->frameMs = frameMs;
        a->floorDb = -62.0f;
        a->dbAboveFloor = QVector<float>(frames, 0.0f);
    }
    for (const Line& l : sc.lines) {
        const int voice = l.voice >= 0 ? l.voice : l.speaker;
        TrackActivity& on = voice == sc.localVoice ? mic : loop;
        const int from = int(l.startMs / frameMs);
        const int to = std::min<int>(frames, int((l.startMs + l.durMs) / frameMs));
        for (int f = from; f < to; ++f) on.dbAboveFloor[f] = 28.0f + float((f * 7) % 5);
    }
    MeetingActivity act;
    act.fingerprint = QStringLiteral("demo");
    act.tracks = {mic, loop};
    return act;
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
    else if (variant == QLatin1String("v3")) sc = v3Scenario();
    else sc = fourSpeakers(variant == QLatin1String("long"));
    if (variant == QLatin1String("none")) sc.transcript = false;

    m_store = std::make_unique<MeetingStore>(m_dir.filePath(QStringLiteral("rec")),
                                             m_dir.filePath(QStringLiteral("meta")));
    m_people = std::make_unique<PeopleStore>(m_dir.filePath(QStringLiteral("meta/people.json")));
    m_prints = std::make_unique<VoiceprintStore>(m_dir.filePath(QStringLiteral("meta/voiceprints.json")));

    Meeting m = m_store->createMeeting(sc.v3 ? QStringLiteral("Negyedéves partnertalálkozó")
                                             : QStringLiteral("Termékcsapat heti egyeztetés"));
    m.durationMs = sc.durationMs;
    if (sc.v3) {
        Track mic;
        mic.id = QStringLiteral("mic");
        mic.deviceName = QStringLiteral("Mikrofon");
        mic.file = QStringLiteral("track_mic.ogg");
        mic.kind = TrackKind::Mic;
        Track loop;
        loop.id = QStringLiteral("loopback");
        loop.deviceName = QStringLiteral("Rendszerhang");
        loop.file = QStringLiteral("track_loopback.ogg");
        loop.kind = TrackKind::Loopback;
        m.tracks = {mic, loop};
        m_tracks = m.tracks;
        m.tagIds = {QStringLiteral("nordvik")};
        m_tagNames.insert(QStringLiteral("nordvik"), QStringLiteral("Nordvik"));
    }

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
    const int dim = sc.voices > 0 ? sc.voices : int(sc.cast.size());
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

    if (sc.v3) m_people->setDefaultSide(QStringLiteral("Fehér Gábor"), QStringLiteral("remote"));

    m_editor = std::make_unique<SpeakerEditor>(m_store.get(), m_people.get(), m_prints.get(), m.id);
    if (sc.v3) {
        m_editor->setUserSpeakerName(QStringLiteral("Kovács Lilla"));
        const auto activity = std::make_shared<const MeetingActivity>(demoActivity(sc));
        m_editor->setActivityProvider([activity](const Meeting&) { return *activity; });
        m_editor->setPersonTagsProvider([](const QString& person) {
            if (person == QStringLiteral("Fehér Gábor") || person == QStringLiteral("Varga Árpád"))
                return QStringList{QStringLiteral("nordvik")};
            if (person == QStringLiteral("Molnár Eszter")) return QStringList{QStringLiteral("piac")};
            return QStringList();
        });
        m_tagNames.insert(QStringLiteral("piac"), QStringLiteral("Piackutatás"));
    }
    if (variant != QLatin1String("novoice")) {
        const auto lines = std::make_shared<const QVector<Line>>(sc.lines);
        const bool noisy = sc.v3;
        m_editor->setEmbedderFactory([lines, dim, noisy] { return std::make_unique<DemoEmbedder>(lines, dim, noisy); });
    }
    m_v3 = sc.v3;
    for (const Line& l : std::as_const(sc.lines)) {
        m_lineSpeaker << l.speaker;
        m_lineVoice << (l.voice >= 0 ? l.voice : l.speaker);
        m_lineDur << l.durMs;
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

bool TranscriptDemoSession::stageV3(const QString& state)
{
    if (!m_v3 || !m_editor || m_editor->utteranceCount() != m_lineSpeaker.size()) return false;
    constexpr int kScript = 9;      // a forgatókönyv eleji (E1–E4) sorok száma
    auto id = [this](int i) { return m_editor->utteranceAt(i).id; };
    // Megerősített mag: az elnevezett beszélők első 4 tiszta, saját hangú sora + az E1 első sora.
    QStringList confirm{id(0)};
    QHash<int, int> per;
    for (int i = kScript; i < m_lineSpeaker.size(); ++i) {
        const int k = m_lineSpeaker[i];
        if (k > 4 || m_lineVoice[i] != k || m_lineDur[i] < 2500 || per.value(k) >= 4) continue;
        ++per[k];
        confirm << id(i);
    }
    // A szennyezett mag (E2 banner): Gábor nevén két Árpád-hangú sor is „Jó így"-t kapott.
    if (state == QLatin1String("reviewGroups") || state == QLatin1String("reviewExpanded")
        || state == QLatin1String("contaminatedCore")) {
        int added = 0;
        for (int i = kScript; i < m_lineSpeaker.size() && added < 2; ++i) {
            if (m_lineSpeaker[i] != 1 || m_lineVoice[i] != 2) continue;
            confirm << id(i);
            ++added;
        }
    }
    m_editor->confirmUtterances(confirm);
    m_editor->setUtterancesNoisy({id(4)}, true);
    // „javítva": Árpád nevén Eszter hangja → kézzel Eszterhez.
    if (state == QLatin1String("markers")) m_editor->moveUtterances({id(3)}, QStringLiteral("Beszélő 4"));
    return true;
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
