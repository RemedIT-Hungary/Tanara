#include "demo/TranscriptEditorDemoFacade.h"

#include "demo/DemoText.h"

#include "tanara/domain/Types.h"
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

using namespace tanara;

namespace tanara_demo {

namespace {

// A script line: who speaks (speaker index), whose VOICE it is (voice; -1 = their own),
// when, until when, what.
struct Line {
    qint64 startMs;
    qint64 durMs;
    int speaker;
    int voice;
    QString text;
};

struct Cast {
    QString raw;        // raw diarization label
    QString person;     // empty = anonymous
    bool voiceprint;
    double weight;      // for the filler lines' distribution
};

// The fake embedder: returns the voice's unit vector from the slice's midpoint (orthogonal
// per speaker), as the core's unit tests do.
class DemoEmbedder : public processing::IUtteranceEmbedder {
public:
    // noisy: the voice vector also gets small deterministic noise beside the speaker's axis (so
    // the evidence percentages of the v3 variant are not all 100% / 0%).
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

// English counterparts of the script lines (the segments get these for an English UI). The
// TIMING of the lines is always computed from the Hungarian text, so the demo states of the
// two languages are identical line by line.
const char* const kLineEn[][2] = {
    // filler
    {"A következő kiadásba ez még belefér, ha a tesztelés időben végez.",
     "This still fits into the next release if testing finishes on time."},
    {"Szerintem előbb nézzük meg a számokat, utána döntsünk.",
     "I'd look at the numbers first and decide after that."},
    {"Rendben, ezt felírom a teendők közé.", "Okay, I'll add that to the action items."},
    {"A súgóoldalak látogatottsága a múlt héten is nőtt, főleg a beállításoknál.",
     "Traffic to the help pages grew again last week, mostly around settings."},
    {"Ezt az ügyfelek oldaláról is megerősítették, két külön megkeresésben is szóba került.",
     "Customers confirmed it too; it came up in two separate requests."},
    {"Akkor a határidő marad péntek, és csütörtökön még egyszer ránézünk.",
     "So the deadline stays Friday, and we'll take another look on Thursday."},
    {"Nálunk a legtöbb kérdés a számlázás körül jön, nem a belépésnél.",
     "On our side most questions are about billing, not about signing in."},
    {"Ha kell, szívesen megmutatom a bontást a megbeszélés végén.",
     "If it helps, I'm happy to show the breakdown at the end of the meeting."},
    {"Egyetértek, de a dokumentációt is frissíteni kell hozzá, különben megint ugyanott akadnak el.",
     "Agreed, but the documentation needs an update too, otherwise people get stuck in the same place again."},
    {"Ezt a pontot hagyjuk a jövő heti tervezésre, ott több időnk lesz rá.",
     "Let's leave this one for next week's planning, we'll have more time for it there."},
    {"A tesztkörnyezet tegnap óta stabil, a két nyitott hibajegy közül az egyik csak kozmetikai.",
     "The test environment has been stable since yesterday, and one of the two open bugs is only cosmetic."},
    {"Jó, akkor én viszem a javítást, és szólok, ha kész.",
     "Good, then I'll take the fix and let you know when it's done."},
    {"A partnereknek érdemes előre jelezni, hogy változik a felület.",
     "We should tell the partners in advance that the interface is changing."},
    {"Még egy kérdés: ez a régi ügyfeleket is érinti, vagy csak az újakat?",
     "One more question: does this affect existing customers too, or only new ones?"},
    {"Csak az újakat, a régieknél marad a mostani folyamat a negyedév végéig.",
     "Only new ones; existing customers keep the current process until the end of the quarter."},
    {"Értem, köszönöm.", "Got it, thanks."},
    {"A keresés javítása hozta a legnagyobb változást: a találatok kétharmada már az első oldalon megvan, "
     "és a visszajelzések szerint a kollégák is ritkábban kérdeznek rá ugyanarra.",
     "The search improvements made the biggest difference: two thirds of the results now show up on the first "
     "page, and according to the feedback, colleagues ask about the same thing far less often."},
    {"Ezt döntésként rögzítsük: a bevezetés két lépésben megy, először a belső csapatnak.",
     "Let's record this as a decision: the rollout happens in two steps, starting with the internal team."},
    {"Igen, így jó lesz.", "Yes, that works."},
    {"A költségvetésnél még visszatérünk rá, addig nem ígérnék pontos dátumot.",
     "We'll come back to it with the budget; until then I wouldn't promise an exact date."},
    // long monologue
    {"Köszönöm, hogy ilyen sokan eljöttetek. Mielőtt belevágunk, szeretném egyben végigmondani, "
     "hol tartunk, mert az elmúlt hetekben sok minden változott, és nem mindenki volt ott minden "
     "egyeztetésen.",
     "Thanks for coming, it's great to see so many of you. Before we dive in, I'd like to walk through where "
     "we are in one go, because a lot has changed in the past few weeks and not everyone was at every meeting."},
    {"Ennyit szerettem volna előrebocsátani; most pedig menjünk végig a részleteken, és a végén "
     "marad idő a kérdésekre is.",
     "That's what I wanted to say up front; now let's go through the details, and there will be time for "
     "questions at the end."},
    // four speakers
    {"Köszönöm, hogy ilyen sokan eljöttetek. Röviden végigmegyek a harmadik negyedéven, utána "
     "szívesen válaszolok a kérdésekre. Az első és legfontosabb: a támogatási jegyek száma "
     "harmadával csökkent, miközben az aktív felhasználók száma nagyjából tizenkét százalékkal "
     "nőtt. Ez azt jelenti, hogy egy felhasználóra most lényegesen kevesebb megkeresés jut, mint "
     "egy évvel ezelőtt, és ezt nem a csapat leépítése okozta, hanem az, hogy a leggyakoribb "
     "kérdéseket végre a termékben válaszoljuk meg.",
     "Thanks for coming, it's great to see so many of you. I'll walk through the third quarter briefly, and "
     "then I'm happy to take questions. First and most important: support tickets dropped by a third, while "
     "active users grew by roughly twelve percent. That means each user now sends far fewer requests than a "
     "year ago, and it isn't because we cut the team, it's because we finally answer the most common "
     "questions inside the product."},
    {"A második pont az új súgóoldalak. Júliusban élesítettük őket, és az első hat hétben a "
     "keresések kétharmada már ott ért véget, nem a jegykezelőben. Ami külön érdekes: a legtöbb "
     "látogatás a beállítási oldalakról érkezik, tehát pont ott, ahol korábban a legtöbben "
     "elakadtak. A következő negyedévben ezt a mintát szeretnénk a számlázásra is átvinni.",
     "The second point is the new help pages. We launched them in July, and in the first six weeks two thirds "
     "of searches already ended there instead of in the ticketing system. What's especially interesting: most "
     "visits come from the settings pages, exactly where most people used to get stuck. Next quarter we'd like "
     "to bring the same pattern to billing."},
    {"Bocs, hogy közbeszólok, ez a szám az összes ügyfélre vonatkozik, vagy csak az újakra?",
     "Sorry to jump in, is that number for all customers, or only the new ones?"},
    {"Az összesre.", "All of them."},
    {"Az újaknál egyébként még jobb az arány, de ott kicsi a minta, ezért nem tettem fel a diára. "
     "Ha érdekel, a végén megmutatom a bontást.",
     "For new customers the ratio is even better, but the sample is small, so I left it off the slide. "
     "If you're interested, I'll show the breakdown at the end."},
    {"Mhm.", "Mhm."},
    {"Ez egyezik azzal, amit a támogatás oldaláról látunk.", "That matches what we see on the support side."},
    {"Egy kérdés a számlázáshoz: ott nem az a gond, hogy a súgó nem elég, hanem hogy maga a "
     "folyamat bonyolult? Szerintem ott a terméket kellene egyszerűsíteni, nem a leírást bővíteni.",
     "A question about billing: isn't the problem there that the process itself is complicated, not that the "
     "help isn't enough? I think we should simplify the product there, not expand the docs."},
    {"Jó kérdés, erre a végén visszatérek, mert van róla egy külön diám.",
     "Good question, I'll come back to it at the end, I have a separate slide on it."},
    {"Elnézést, megismételnéd az utolsó mondatot? Itt akadozott a hang.",
     "Sorry, could you repeat the last sentence? The audio cut out on my end."},
    {"Persze. Azt mondtam, hogy a számlázásra is átvinnénk a súgót, de Nórának igaza van abban, "
     "hogy ott a folyamat maga is túl hosszú. A terv ezért két lépésből áll: először "
     "összegyűjtjük, hol akadnak el a legtöbben, aztán eldöntjük, mit javítunk a termékben és "
     "mit a leírásban.",
     "Sure. I said we'd bring the help pages to billing as well, but Olivia is right that the process itself "
     "is too long there. So the plan has two steps: first we collect where most people get stuck, then we "
     "decide what to fix in the product and what in the docs."},
    // two speakers
    {"Akkor kezdjük az elején: mióta használjátok a jelenlegi jegykezelőt?",
     "Let's start at the beginning: how long have you been using your current ticketing system?"},
    {"Nagyjából három éve. Előtte e-mailből dolgoztunk, ami tíz ember fölött már követhetetlen volt.",
     "About three years. Before that we worked from email, which became impossible to follow above ten people."},
    {"Az első évben főleg a címkézéssel küzdöttünk, mindenki máshogy használta.",
     "In the first year we mostly struggled with labels, everyone used them differently."},
    {"Aztán bevezettünk egy közös listát, és onnantól sokkal jobb lett.",
     "Then we introduced a shared list, and it got much better from there."},
    {"Ma már az új kollégák is egy hét alatt beletanulnak.", "Today new colleagues pick it up within a week."},
    {"És mi az, ami most a legjobban hiányzik belőle?", "And what do you miss most in it right now?"},
    {"Hm, talán a jobb keresés.", "Hm, better search, maybe."},
    {"Meg az, hogy a hívásokból ne nekünk kelljen kézzel jegyet írni. Ezért is néztük a Tanarát.",
     "And not having to write tickets by hand from our calls. That's partly why we looked at Tanara."},
    // many speakers
    {"A második negyedévben a támogatási jegyek száma harmadával csökkent.",
     "In the second quarter, support tickets dropped by a third."},
    {"Ez az új súgóoldalaknak köszönhető, vagy annak, hogy kevesebb az ügyfél?",
     "Is that thanks to the new help pages, or because there are fewer customers?"},
    {"Mindkettőnek, de inkább a súgónak.", "Both, but mostly the help pages."},
    {"Nálunk is ezt látjuk.", "We're seeing the same."},
    {"Akkor a súgót a többi termékre is kiterjesztjük.", "Then we'll extend the help pages to the other products too."},
    {"Ehhez kellene még egy fő a dokumentációra.", "For that we'd need one more person on documentation."},
    {"Ezt a költségvetésnél nézzük meg.", "Let's look at that with the budget."},
    {"Rendben, felírom.", "Okay, noting it down."},
    {"Én a kiadás felől nézném: mi fér bele a mostani körbe?",
     "I'd look at it from the release side: what fits into the current round?"},
    {"A tesztkörnyezet felől nincs akadálya.", "Nothing blocking it on the test environment side."},
    {"Halljátok? Most csatlakoztam.", "Can you hear me? I just joined."},
    {"Igen, jól hallunk, köszi.", "Yes, we can hear you fine, thanks."},
    // v3 (handoff-v3)
    {"Igen, és ide tartozik még, hogy a számlázási rész lesz a következő; ott a legtöbb a visszakérdezés.",
     "Yes, and on top of that, billing comes next; that's where we get the most follow-up questions."},
    {"Pontosan.", "Exactly."},
    {"Igen, ezt mi is láttuk a partnerportálon, főleg a Nordvik-ügyfeleknél, ott a jegyek fele számlázási volt.",
     "Yes, we saw that on the partner portal too, especially with Nordvik customers, half of their tickets were about billing."},
    {"A mi oldalunkon ez a negyedik negyedév elejére kellene, különben a megújításnál újra elő fog jönni.",
     "On our side we'd need this by early Q4, otherwise it will come up again at renewal."},
    {"Ezt a részt én vállalom, de kellene hozzá két hét a dokumentációs csapattól.",
     "I'll take this part, but I'd need two weeks from the documentation team."},
    {"Értem.", "I see."},
    {"És mikorra várható a számlázási súgó? Az ügyfeleink már kérdezik.",
     "And when can we expect the billing help? Our customers are already asking."},
    {"Én a pénzügy felől jövök: nálunk a számlázási kérdések fele a díjbekérőkről szól, nem magáról a számláról.",
     "I'm coming from finance: half of our billing questions are about pro forma invoices, not the invoice itself."},
    {"Ha a súgó ezt külön kezelné, szerintem a maradék is eltűnne.",
     "If the help covered that separately, I think the rest would go away too."},
    {"Igen.", "Yes."},
    {"Jó.", "Good."},
    {"Aha.", "Uh-huh."},
};

// The line's text in the demo language (an unknown line stays unchanged).
QString lineText(const QString& hu)
{
    if (!demotext::english()) return hu;
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        for (const auto& p : kLineEn) m.insert(QString::fromUtf8(p[0]), QString::fromUtf8(p[1]));
        return m;
    }();
    return map.value(hu, hu);
}

// Filler sentences (fictional, generic product-team meeting).
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
    // A very long utterance (~280 words) for the "long monologue" state.
    QStringList parts;
    parts << QStringLiteral("Köszönöm, hogy ilyen sokan eljöttetek. Mielőtt belevágunk, szeretném egyben végigmondani, "
                            "hol tartunk, mert az elmúlt hetekben sok minden változott, és nem mindenki volt ott minden "
                            "egyeztetésen.");
    const QStringList& pool = fillerPool();
    for (int i = 0; i < 22; ++i) parts << pool[(i * 7 + 3) % pool.size()];
    parts << QStringLiteral("Ennyit szerettem volna előrebocsátani; most pedig menjünk végig a részleteken, és a végén "
                            "marad idő a kérdésekre is.");
    for (QString& p : parts) p = lineText(p);
    return parts.join(QLatin1Char(' '));
}

struct Scenario {
    QVector<Cast> cast;
    QVector<Line> lines;
    int seedLine = -1;      // the suggestion state's "to be moved by hand" row
    int seedTarget = -1;
    qint64 durationMs = 0;
    bool transcript = true;
    // v3: tracks (mic + call audio) with fictional activity, tags, a voice outside the cast (the
    // "new person"), noisy voice vectors.
    bool v3 = false;
    int voices = 0;     // the voice dimension (0 = the cast size)
    int localVoice = 0; // this voice's lines are on the mic (the others on the call)
};

// Filler: from the given time to the end of the meeting, with a weighted speaker order. `stray`:
// (speaker, voice) pairs that occasionally speak in the "wrong voice" (uncertain rows).
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
        // Per turn 1-3 utterances (paragraphs) from the same speaker.
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
        // Raw "Remote 1", but by voice Fehér Ádám: the suggestion state moves this.
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
        // The long monologue pushes the other rows out.
        const qint64 shift = 95000;
        sc.lines[0].durMs += shift;
        for (int i = 1; i < sc.lines.size(); ++i) sc.lines[i].startMs += shift;
    }
    sc.seedLine = 6;
    sc.seedTarget = 1;
    const qint64 from = sc.lines.last().startMs + sc.lines.last().durMs + 1500;
    // The "Remote 1" label occasionally speaks in Fehér Ádám's voice (these get the suggestion);
    // single rows of Varga Nóra are in Kovács Lilla's voice (plain uncertain rows).
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
        // Everyone speaks once so that the 11 speakers are certainly present.
        {ts(0, 40), 3000, 1, -1, QStringLiteral("Én a kiadás felől nézném: mi fér bele a mostani körbe?")},
        {ts(0, 44), 2600, 7, -1, QStringLiteral("A tesztkörnyezet felől nincs akadálya.")},
        {ts(0, 48), 2400, 8, -1, QStringLiteral("Halljátok? Most csatlakoztam.")},
        {ts(0, 52), 2400, 10, -1, QStringLiteral("Igen, jól hallunk, köszi.")},
    };
    fill(sc, ts(0, 57), ts(52, 10), 19, {{2, 0}, {5, 1}}, 29);
    return sc;
}

// The v3 meeting: Kovács Lilla on her own mic, the others on the call audio. At the start
// of the script the rows for the new-person flow; then filler, including the "wrong" rows
// for the groups: Gábor's voice from the call under Lilla's name (track contradiction),
// Árpád's voice under Gábor's name (core deviation), a voice outside the cast under
// Gábor's name (Nagy Péter, the new person), and short rows.
Scenario v3Scenario()
{
    Scenario sc;
    sc.v3 = true;
    sc.voices = 7;          // 6 speakers + Nagy Péter's voice (6)
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
        // Under Lilla's name, but Gábor's voice from the call: "uncertain, track".
        {ts(0, 13), 7400, 0, 1,
         QStringLiteral("Igen, ezt mi is láttuk a partnerportálon, főleg a Nordvik-ügyfeleknél, ott a jegyek fele számlázási volt.")},
        // Under Árpád's name, Eszter's voice: the "markers" state moves it by hand (fixed). (Árpád's
        // raw label thus appears before Eszter's: the color order follows the handoff.)
        {ts(0, 22), 6600, 2, 3,
         QStringLiteral("A mi oldalunkon ez a negyedik negyedév elejére kellene, különben a megújításnál újra elő fog jönni.")},
        {ts(0, 30), 5200, 1, -1,
         QStringLiteral("Ezt a részt én vállalom, de kellene hozzá két hét a dokumentációs csapattól.")},
        {ts(0, 36), 900, 3, -1, QStringLiteral("Értem.")},
        // Under Gábor's name, but Nagy Péter's voice (these three go to the new person).
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
                if (k == 0 && own % 4 == 0) l.voice = 1;            // Gábor under Lilla's name (on the call)
                else if (k == 1 && own % 5 == 0) l.voice = 2;       // Árpád under Gábor's name
                else if (k == 1 && own % 11 == 0) l.voice = 6;      // Nagy Péter under Gábor's name
            }
            sc.lines.append(l);
            t += l.durMs + 400 + qint64(rnd() * 2400);
        }
    }
    sc.durationMs = until;
    return sc;
}

// The fictional track activity: during the row, the mic OR the call track sounds (by the
// voice's owner), the other is quiet.
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

TranscriptEditorDemoFacade::TranscriptEditorDemoFacade(const QString& variant, QObject* parent)
    : ITranscriptEditorFacade(parent)
{
    Scenario sc;
    if (variant == QLatin1String("two")) sc = twoSpeakers();
    else if (variant == QLatin1String("many")) sc = manySpeakers();
    else if (variant == QLatin1String("v3")) sc = v3Scenario();
    else sc = fourSpeakers(variant == QLatin1String("long"));
    if (variant == QLatin1String("none")) sc.transcript = false;
    // For the English demo language: English counterparts of the names and raw labels (order,
    // weights and voice indices unchanged; the row texts are translated when segments are written).
    for (Cast& c : sc.cast) {
        c.raw = demoRaw(c.raw);
        c.person = demoName(c.person);
    }

    m_store = std::make_unique<library::MeetingRepository>(m_dir.filePath(QStringLiteral("rec")),
                                             m_dir.filePath(QStringLiteral("meta")));
    m_people = std::make_unique<library::PeopleStore>(m_dir.filePath(QStringLiteral("meta/people.json")));
    m_prints = std::make_unique<library::VoiceprintStore>(m_dir.filePath(QStringLiteral("meta/voiceprints.json")));

    Meeting m = m_store->createMeeting(sc.v3 ? demoText("Negyedéves partnertalálkozó", "Quarterly partner review")
                                             : demoText("Termékcsapat heti egyeztetés", "Product team weekly sync"));
    m.durationMs = sc.durationMs;
    if (sc.v3) {
        Track mic;
        mic.id = QStringLiteral("mic");
        mic.deviceName = demoText("Mikrofon", "Microphone");
        mic.file = QStringLiteral("track_mic.ogg");
        mic.kind = TrackKind::Mic;
        Track loop;
        loop.id = QStringLiteral("loopback");
        loop.deviceName = demoText("Rendszerhang", "System audio");
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
            o[QStringLiteral("text")] = lineText(l.text);
            segs.append(o);
        }
        writeJson(editing::speakeredit::segmentsPath(m.folder), QJsonDocument(segs));
        m.hasTranscript = true;
    }

    // Names + voiceprints (for the lane header dot) + a voice-identification score.
    const int dim = sc.voices > 0 ? sc.voices : int(sc.cast.size());
    for (int k = 0; k < sc.cast.size(); ++k) {
        const Cast& c = sc.cast[k];
        if (c.person.isEmpty()) continue;
        const PersonId person = m_people->ensure(c.person);
        m.speakerMap.insert(c.raw, person);
        if (c.voiceprint) {
            Voiceprint p;
            p.embedding = QVector<float>(dim, 0.0f);
            p.embedding[k] = 1.0f;
            p.sourceMeetingId = m.id;
            m_prints->addPrint(person, p);
        }
    }
    m_store->saveMeeting(m);
    if (sc.transcript && !sc.cast.isEmpty() && !sc.cast[0].person.isEmpty())
        editing::speakeredit::recordIdentification(m.folder, sc.cast[0].raw, m_people->idForName(sc.cast[0].person), 0.82);

    auto idOf = [this](const QString& name) { return m_people->ensure(name); };
    if (sc.v3) m_people->setDefaultSide(idOf(demoName("Fehér Gábor")), QStringLiteral("remote"));

    m_session = std::make_unique<editing::SpeakerEditSession>(m_store.get(), m_people.get(), m_prints.get(), m.id);
    m_recheck = std::make_unique<editing::RecheckService>(m_session.get());
    // The recheck's feedback names the voice models as the application does ("CAM++").
    m_recheck->setModelNameResolver([](const QString& id) {
        const auto spec = processing::VoiceModelRegistry::spec(id);
        return spec ? spec->displayName : QString();
    });
    if (sc.v3) {
        m_selfName = demoName("Kovács Lilla");
        m_selfId = idOf(m_selfName);
        m_people->setSelfId(m_selfId);
        m_session->setSelfPerson(SelfPerson{m_selfId, m_selfName});
        const auto activity = std::make_shared<const MeetingActivity>(demoActivity(sc));
        m_session->setActivityProvider([activity](const Meeting&) { return *activity; });
        m_personTags->insert(idOf(demoName("Fehér Gábor")), {QStringLiteral("nordvik")});
        m_personTags->insert(idOf(demoName("Varga Árpád")), {QStringLiteral("nordvik")});
        m_personTags->insert(idOf(demoName("Molnár Eszter")), {QStringLiteral("piac")});
        const auto tags = m_personTags;
        m_session->setPersonTagsProvider([tags](const PersonId& person) { return tags->value(person); });
        // Tag evidence from fictional history: of the 14 meetings tagged #Nordvik, Gábor was in 11
        // (manual tag too), Árpád in 6 (learned only); Eszter's manual tag is
        // #MarketResearch, she was never at a #Nordvik meeting -> contradicts.
        library::PersonTagSnapshot snap;
        QVector<library::PersonTagRow> rows;
        for (int i = 0; i < 14; ++i) {
            PersonIds who;
            if (i < 11) who << idOf(demoName("Fehér Gábor"));
            if (i < 6) who << idOf(demoName("Varga Árpád"));
            rows << library::PersonTagRow{QStringLiteral("demo-n%1").arg(i), QDateTime(), {QStringLiteral("nordvik")}, who};
        }
        for (int i = 0; i < 3; ++i)
            rows << library::PersonTagRow{QStringLiteral("demo-p%1").arg(i), QDateTime(), {QStringLiteral("piac")},
                                 {idOf(demoName("Molnár Eszter"))}};
        snap.stats = std::make_shared<library::PersonTagStats>(library::PersonTagStats::compute(rows, m_selfId));
        snap.tagNames = {{QStringLiteral("nordvik"), QStringLiteral("Nordvik")},
                         {QStringLiteral("piac"), demoText("Piackutatás", "Market research")}};
        snap.self = m_selfId;
        snap.names = m_people->names();
        // The manual tags come from the demo's live map (the quick "+ #Tag" writes there), the
        // learned statistics from the fictional history above.
        m_session->setTagEvidenceProvider([snap, tags](const PersonId& person, const QStringList& meetingTags,
                                                      const QString& mid) {
            library::PersonTagSnapshot live = snap;
            live.manual = *tags;
            return live.evidence(person, meetingTags, mid);
        });
        m_tagNames.insert(QStringLiteral("piac"), demoText("Piackutatás", "Market research"));
    }
    if (variant != QLatin1String("novoice")) {
        const auto lines = std::make_shared<const QVector<Line>>(sc.lines);
        const bool noisy = sc.v3;
        const auto factory = [lines, dim, noisy] { return std::make_unique<DemoEmbedder>(lines, dim, noisy); };
        m_session->setEmbedderFactory(factory);
        m_recheck->setEmbedderFactory(factory);
    }
    m_v3 = sc.v3;
    for (const Line& l : std::as_const(sc.lines)) {
        m_lineSpeaker << l.speaker;
        m_lineVoice << (l.voice >= 0 ? l.voice : l.speaker);
        m_lineDur << l.durMs;
    }
    if (sc.transcript && sc.seedLine >= 0) {
        m_seedId = m_session->utteranceAt(sc.seedLine).id;
        m_seedTarget = sc.cast[sc.seedTarget].raw;
    }
}

bool TranscriptEditorDemoFacade::addPersonTag(const PersonId& person, const QString& tagId)
{
    if (person.isNull() || tagId.isEmpty() || person == m_selfId) return false;
    QStringList& tags = (*m_personTags)[person];
    if (!tags.contains(tagId)) tags << tagId;
    return true;
}

TranscriptEditorDemoFacade::~TranscriptEditorDemoFacade()
{
    // Waiting for the background thread before the stores go away; the service works on the session.
    m_recheck.reset();
    m_session.reset();
}

editing::ITranscriptEditorFacade::Session TranscriptEditorDemoFacade::open(const QString&)
{
    Session s;
    s.session = m_session.get();
    s.recheck = m_recheck.get();
    return s;
}

QVector<library::PersonTagQuestion> TranscriptEditorDemoFacade::newPersonTagQuestions(const QString&,
                                                                                     const PersonId& person,
                                                                                     const QString& name)
{
    if (!m_tagQuestion) return {};
    return {library::PersonTagQuestion{QStringLiteral("nordvik"), QStringLiteral("Nordvik"), {person},
                                       {name.trimmed()}, true}};
}

bool TranscriptEditorDemoFacade::stage(const QString& state)
{
    m_tagQuestion = state == QLatin1String("newPersonTag");
    if (!m_v3 || !m_session || m_session->utteranceCount() != m_lineSpeaker.size()) return false;
    constexpr int kScript = 9;      // number of rows at the script's start (new-person flow)
    auto id = [this](int i) { return m_session->utteranceAt(i).id; };
    // Confirmed core: the named speakers' first 4 clean, own-voice rows + the first row of the new-person flow.
    QStringList confirm{id(0)};
    QHash<int, int> per;
    for (int i = kScript; i < m_lineSpeaker.size(); ++i) {
        const int k = m_lineSpeaker[i];
        if (k > 4 || m_lineVoice[i] != k || m_lineDur[i] < 2500 || per.value(k) >= 4) continue;
        ++per[k];
        confirm << id(i);
    }
    // The contaminated core (banner): two Árpád-voice rows under Gábor's name were also given "Looks right".
    if (state == QLatin1String("reviewGroups") || state == QLatin1String("reviewExpanded")
        || state == QLatin1String("contaminatedCore")) {
        int added = 0;
        for (int i = kScript; i < m_lineSpeaker.size() && added < 2; ++i) {
            if (m_lineSpeaker[i] != 1 || m_lineVoice[i] != 2) continue;
            confirm << id(i);
            ++added;
        }
    }
    m_session->confirmUtterances(confirm);
    m_session->setUtterancesNoisy({id(4)}, true);
    // "fixed": Eszter's voice under Árpád's name -> moved to Eszter by hand.
    if (state == QLatin1String("markers")) m_session->moveUtterances({id(3)}, demoRaw(QStringLiteral("Beszélő 4")));
    if (state == QLatin1String("speakerWhy")) {
        // Gábor speaks on two tracks too (manual assignment, several tracks); Árpád has no #Nordvik
        // yet (the quick "+ #Nordvik" shows on the "NOT them?" candidate).
        for (const editing::EditorSpeaker& sp : m_session->speakers())
            if (sp.personId == m_people->idForName(demoName("Fehér Gábor")))
                m_session->setTrackAssignment(sp.key, {QStringLiteral("mic"), QStringLiteral("loopback")});
        m_personTags->remove(m_people->idForName(demoName("Varga Árpád")));
        m_session->notifyPersonTagsChanged();
    }
    return true;
}

QVector<editing::PersonInfo> TranscriptEditorDemoFacade::people() const
{
    // Fictional name list for the pickers; the meeting's people with their real voiceprint state.
    struct P { const char* name; bool vp; int meetings; };
    static const P extra[] = {
        {"Balogh Kata", true, 12}, {"Bálint Péter", false, 3}, {"Baranyi Zsófia", true, 7},
        {"Molnár Eszter", true, 9}, {"Kovács Lilla", true, 14}, {"Fehér Ádám", true, 11},
        {"Varga Nóra", true, 8}, {"Tóth Bence", true, 6}, {"Szabó Áron", false, 4},
        {"Németh Dávid", true, 5}, {"Horváth Gergő", false, 2}, {"Ördög Ödön", false, 1},
    };
    QVector<editing::PersonInfo> out;
    QStringList seen;
    for (const P& p : extra) {
        editing::PersonInfo i;
        i.name = demoName(p.name);
        i.personId = m_people->idForName(i.name);   // null: a fictional name not in the demo store yet
        i.voiceprintCount = m_prints->printCount(i.personId);
        i.hasVoiceprint = p.vp || i.voiceprintCount > 0;
        if (i.hasVoiceprint && i.voiceprintCount == 0) i.voiceprintCount = 1;
        i.meetingCount = p.meetings;
        out.append(i);
        seen << i.name;
    }
    // People added in the demo (people.json) should show up too.
    for (const Person& person : m_people->people()) {
        if (seen.contains(person.displayName)) continue;
        editing::PersonInfo i;
        i.personId = person.id;
        i.name = person.displayName;
        i.voiceprintCount = m_prints->printCount(person.id);
        i.hasVoiceprint = i.voiceprintCount > 0;
        i.meetingCount = 1;
        out.append(i);
    }
    std::sort(out.begin(), out.end(), [](const editing::PersonInfo& a, const editing::PersonInfo& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

} // namespace tanara_demo
