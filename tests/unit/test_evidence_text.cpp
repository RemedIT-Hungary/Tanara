// Evidence keys: every key renders at display time in Hungarian (source) and English (from the shipped
// i18n/tanara_en.ts), the params are substituted, the JSON keeps key + params and no text, an old text-only
// file is mapped to the best-matching key (unknown text stays verbatim as "legacy"), and the participant
// analysis now produces keys instead of translated text.
#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QTranslator>
#include <QXmlStreamReader>

#include "tanara/domain/EvidenceKeys.h"
#include "tanara/domain/EvidenceText.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"

using namespace tanara;
namespace K = tanara::evidencekeys;

namespace {

// A translator fed from the "EvidenceText" context of the shipped .ts (the test exe embeds no .qm).
class TsTranslator : public QTranslator {
public:
    bool loadTs(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QXmlStreamReader x(&f);
        QString ctx, src;
        while (!x.atEnd()) {
            x.readNext();
            if (!x.isStartElement()) continue;
            if (x.name() == u"name") ctx = x.readElementText(QXmlStreamReader::IncludeChildElements);
            else if (x.name() == u"source") src = x.readElementText(QXmlStreamReader::IncludeChildElements);
            else if (x.name() == u"translation") {
                const bool unfinished = x.attributes().value(u"type") == u"unfinished";
                const QString tr = x.readElementText(QXmlStreamReader::IncludeChildElements);
                if (!unfinished && !tr.isEmpty()) m_map.insert(ctx + QLatin1Char('\x1f') + src, tr);
                else m_unfinished.append(ctx + QLatin1Char('\x1f') + src);
            }
        }
        return true;
    }
    QString translate(const char* context, const char* source, const char*, int) const override
    {
        return m_map.value(QString::fromUtf8(context) + QLatin1Char('\x1f') + QString::fromUtf8(source));
    }
    bool isEmpty() const override { return false; }
    QStringList m_unfinished;
private:
    QHash<QString, QString> m_map;
};

Evidence ev(EvidenceKind kind, Polarity pol, double value, const QLatin1String& key, const QVariantMap& params = {})
{
    Evidence e;
    e.kind = kind;
    e.polarity = pol;
    e.value = value;
    e.key = key;
    e.params = params;
    return e;
}

QVariantMap tagItem(const QString& name, int shared, int total, bool manual)
{
    return {{"name", name}, {"shared", shared}, {"total", total}, {"manual", manual}};
}

struct Sample { Evidence e; QString hu; QString en; };

QVector<Sample> samples()
{
    const QVariantMap mic{{K::kParamSides, QStringList{"mic"}}};
    const QVariantMap both{{K::kParamSides, QStringList{"mic", "loopback"}}};
    const QVariantMap call{{K::kParamSides, QStringList{"loopback"}}};
    return {
        {ev(EvidenceKind::Voice, Polarity::Support, 0.824, K::kVoiceScore), "hang 82%", "voice 82%"},
        {ev(EvidenceKind::Voice, Polarity::Neutral, 0.4, K::kVoiceUnknown), "ismeretlen hang", "unknown voice"},
        {ev(EvidenceKind::Voice, Polarity::Contradict, 0.1, K::kVoiceNotHeard), "nem hallottuk", "not heard"},
        {ev(EvidenceKind::Voice, Polarity::Neutral, 0.0, K::kVoiceNoVoiceprint), "nincs lenyomata", "no voiceprint"},
        {ev(EvidenceKind::Side, Polarity::Support, 1.0, K::kSideHeard, mic), "mikrofon", "microphone"},
        {ev(EvidenceKind::Side, Polarity::Neutral, 0.0, K::kSideHeard, both), "mikrofon és hívás", "microphone and call"},
        {ev(EvidenceKind::Side, Polarity::Contradict, 0.0, K::kSideCallOnly), "csak a hívás hangján", "only on the call audio"},
        {ev(EvidenceKind::Side, Polarity::Support, 1.0, K::kManualTracks, call), "kézzel: hívás", "by hand: call"},
        {ev(EvidenceKind::Manual, Polarity::Support, 1.0, K::kManualAdded), "kézzel felvéve", "added by hand"},
        {ev(EvidenceKind::Tag, Polarity::Support, 2, K::kTagMatch, {{K::kParamChip, "#Projekt +1"}}), "#Projekt +1", "#Projekt +1"},
        {ev(EvidenceKind::Tag, Polarity::Neutral, 1, K::kTagOther), "más címkék", "other tags"},
        {ev(EvidenceKind::Voice, Polarity::Neutral, 0.0, K::kLegacy, {{K::kParamText, "valami régi"}}), "valami régi", "valami régi"},
    };
}

} // namespace

class EvidenceTextTest : public QObject {
    Q_OBJECT
private slots:
    void everyKeyRendersInBothLanguages();
    void percentIsRoundedAndClamped();
    void tagDetailRendersNumbersAndNames();
    void keyWithoutKeyKeepsTransientText();
    void jsonKeepsKeyAndParamsNotText();
    void legacyTextMapsToKeys();
    void unknownLegacyTextStaysVerbatim();
    void analysisProducesKeysNotText();
    void englishTsHasNoUnfinishedEvidenceText();
};

void EvidenceTextTest::everyKeyRendersInBothLanguages()
{
    for (const Sample& s : samples())
        QCOMPARE(evidenceText(s.e), s.hu);

    TsTranslator en;
    QVERIFY(en.loadTs(QStringLiteral(TANARA_EN_TS)));
    QCoreApplication::installTranslator(&en);
    for (const Sample& s : samples())
        QCOMPARE(evidenceText(s.e), s.en);
    QCOMPARE(evidenceSides(samples().at(5).e), (QStringList{"mic", "loopback"}));
    QCoreApplication::removeTranslator(&en);
    QCOMPARE(evidenceText(samples().first().e), QStringLiteral("hang 82%"));   // back to the source language
}

void EvidenceTextTest::percentIsRoundedAndClamped()
{
    QCOMPARE(evidenceText(ev(EvidenceKind::Voice, Polarity::Support, 1.7, K::kVoiceScore)), QStringLiteral("hang 100%"));
    QCOMPARE(evidenceText(ev(EvidenceKind::Voice, Polarity::Support, -0.2, K::kVoiceScore)), QStringLiteral("hang 0%"));
    QCOMPARE(evidenceText(ev(EvidenceKind::Voice, Polarity::Support, 0.805, K::kVoiceScore)), QStringLiteral("hang 81%"));
}

void EvidenceTextTest::tagDetailRendersNumbersAndNames()
{
    // Manual tag, one item.
    Evidence m = ev(EvidenceKind::Tag, Polarity::Support, 1, K::kTagMatch,
                    {{K::kParamChip, "#Nordvik"}, {K::kParamPerson, "Fehér Gábor"},
                     {K::kParamItems, QVariantList{tagItem("Nordvik", 11, 14, true)}}});
    QCOMPARE(evidenceDetail(m), QStringLiteral("Fehér Gábor címkéje: #Nordvik (11 / 14 megbeszélésen volt ott)."));
    // Learned, two items: the shared-tags sentence is appended.
    Evidence l = ev(EvidenceKind::Tag, Polarity::Support, 2, K::kTagMatch,
                    {{K::kParamChip, "#A +1"},
                     {K::kParamItems, QVariantList{tagItem("A", 5, 6, false), tagItem("B", 2, 3, false)}}});
    QCOMPARE(evidenceDetail(l), QStringLiteral("A #A megbeszéléseken szokott lenni (5 / 6). Közös címkék: #A 5 / 6, #B 2 / 3."));
    // Contradiction, with and without the person's other tags.
    Evidence o = ev(EvidenceKind::Tag, Polarity::Neutral, 1, K::kTagOther,
                    {{K::kParamItems, QVariantList{tagItem("X", 0, 9, false)}}, {K::kParamElsewhere, QStringList{"Y"}}});
    QCOMPARE(evidenceDetail(o), QStringLiteral("Más címkéken szokott lenni: #Y. A #X 9 megbeszéléséből egyiken sem volt."));
    o.params.remove(K::kParamElsewhere);
    QCOMPARE(evidenceDetail(o), QStringLiteral("A #X 9 megbeszéléséből egyiken sem volt."));

    TsTranslator en;
    QVERIFY(en.loadTs(QStringLiteral(TANARA_EN_TS)));
    QCoreApplication::installTranslator(&en);
    QVERIFY(evidenceDetail(m).contains(QStringLiteral("Fehér Gábor")));
    QVERIFY(evidenceDetail(m) != QStringLiteral("Fehér Gábor címkéje: #Nordvik (11 / 14 megbeszélésen volt ott)."));
    QVERIFY(evidenceDetail(l) != QStringLiteral("A #A megbeszéléseken szokott lenni (5 / 6). Közös címkék: #A 5 / 6, #B 2 / 3."));
    QVERIFY(evidenceDetail(o) != QStringLiteral("A #X 9 megbeszéléséből egyiken sem volt."));
    QCoreApplication::removeTranslator(&en);
}

void EvidenceTextTest::keyWithoutKeyKeepsTransientText()
{
    Evidence e;
    e.text = QStringLiteral("hasonló hang: Bence");
    e.detail = QStringLiteral("a két hang egyezik");
    QCOMPARE(evidenceText(e), e.text);
    QCOMPARE(evidenceDetail(e), e.detail);
    QVERIFY(evidenceSides(e).isEmpty());
}

void EvidenceTextTest::jsonKeepsKeyAndParamsNotText()
{
    for (const Sample& s : samples()) {
        const QJsonObject o = library::toJson(s.e);
        QVERIFY(!o.contains(QStringLiteral("text")));
        QCOMPARE(o.value(QStringLiteral("key")).toString(), s.e.key);
        const Evidence back = library::evidenceFromJson(QJsonDocument(o).object());
        QCOMPARE(back.key, s.e.key);
        QCOMPARE(back.kind, s.e.kind);
        QCOMPARE(back.polarity, s.e.polarity);
        QCOMPARE(evidenceText(back), s.hu);
        QCOMPARE(evidenceSides(back), evidenceSides(s.e));
    }
    // Tag items survive the number -> JSON double -> int trip.
    Evidence t = ev(EvidenceKind::Tag, Polarity::Support, 1, K::kTagMatch,
                    {{K::kParamChip, "#N"}, {K::kParamItems, QVariantList{tagItem("N", 11, 14, true)}}, {K::kParamPerson, "P"}});
    const Evidence back = library::evidenceFromJson(library::toJson(t));
    QCOMPARE(evidenceDetail(back), evidenceDetail(t));
}

void EvidenceTextTest::legacyTextMapsToKeys()
{
    struct Row { const char* kind; const char* text; QLatin1String key; QString shown; };
    const QVector<Row> rows = {
        {"manual", "kézzel felvéve", K::kManualAdded, "kézzel felvéve"},
        {"manual", "added by hand", K::kManualAdded, "kézzel felvéve"},
        {"voice", "hang 82%", K::kVoiceScore, "hang 82%"},
        {"voice", "voice 82%", K::kVoiceScore, "hang 82%"},
        {"voice", "ismeretlen hang", K::kVoiceUnknown, "ismeretlen hang"},
        {"voice", "unknown voice", K::kVoiceUnknown, "ismeretlen hang"},
        {"voice", "nem hallottuk", K::kVoiceNotHeard, "nem hallottuk"},
        {"voice", "nincs lenyomata", K::kVoiceNoVoiceprint, "nincs lenyomata"},
        {"side", "mikrofon", K::kSideHeard, "mikrofon"},
        {"side", "microphone", K::kSideHeard, "mikrofon"},
        {"side", "hívás", K::kSideHeard, "hívás"},
        {"side", "call", K::kSideHeard, "hívás"},
        {"side", "mikrofon és hívás", K::kSideHeard, "mikrofon és hívás"},
        {"side", "microphone and call", K::kSideHeard, "mikrofon és hívás"},
        {"side", "kézzel: hívás", K::kManualTracks, "kézzel: hívás"},
        {"side", "by hand: microphone and call", K::kManualTracks, "kézzel: mikrofon és hívás"},
        {"side", "csak a hívás hangján", K::kSideCallOnly, "csak a hívás hangján"},
        {"tag", "#Projekt", K::kTagMatch, "#Projekt"},
        {"tag", "#Projekt +2", K::kTagMatch, "#Projekt +2"},
        {"tag", "más címkék", K::kTagOther, "más címkék"},
        {"tag", "other tags", K::kTagOther, "más címkék"},
    };
    for (const Row& r : rows) {
        QJsonObject o{{"kind", r.kind}, {"polarity", "support"}, {"value", 0.82}, {"text", r.text}, {"detail", "régi tooltip"}};
        const Evidence e = library::evidenceFromJson(o);
        QVERIFY2(e.key == r.key, r.text);
        QCOMPARE(evidenceText(e), r.shown);
        QCOMPARE(evidenceDetail(e), QStringLiteral("régi tooltip"));   // an old detail is shown verbatim
    }
    const Evidence side = library::evidenceFromJson({{"kind", "side"}, {"text", "mikrofon és hívás"}});
    QCOMPARE(evidenceSides(side), (QStringList{"mic", "loopback"}));
}

void EvidenceTextTest::unknownLegacyTextStaysVerbatim()
{
    for (const char* kind : {"voice", "side", "tag", "calendar", "lineCount"}) {
        const Evidence e = library::evidenceFromJson({{"kind", kind}, {"text", "meghívva a naptárból"}});
        QCOMPARE(e.key, K::kLegacy);
        QCOMPARE(evidenceText(e), QStringLiteral("meghívva a naptárból"));
    }
    // A "mikrofon" text of the wrong kind is not a side badge.
    QCOMPARE(library::evidenceFromJson({{"kind", "voice"}, {"text", "mikrofon"}}).key, K::kLegacy);
}

void EvidenceTextTest::analysisProducesKeysNotText()
{
    Meeting m;
    processing::participants::CandidateContext ctx;
    Participant p;
    p.personId = PersonId::fromString(QStringLiteral("p1"));
    p.sides = {QStringLiteral("mic")};
    const QVector<Evidence> heard = processing::participants::participantEvidence(p, true, 0.91, m, ctx);
    QVERIFY(!heard.isEmpty());
    for (const Evidence& e : heard) {
        QVERIFY(!e.key.isEmpty());
        QVERIFY(e.text.isEmpty());
    }
    QCOMPARE(heard.first().key, K::kVoiceScore);
    QCOMPARE(evidenceText(heard.first()), QStringLiteral("hang 91%"));
    p.source = ParticipantSource::Manual;
    const QVector<Evidence> manual = processing::participants::participantEvidence(p, false, -1.0, m, ctx);
    QCOMPARE(manual.first().key, K::kManualAdded);
    QCOMPARE(manual.at(1).key, K::kVoiceNoVoiceprint);
}

void EvidenceTextTest::englishTsHasNoUnfinishedEvidenceText()
{
    TsTranslator en;
    QVERIFY(en.loadTs(QStringLiteral(TANARA_EN_TS)));
    for (const QString& u : en.m_unfinished) QVERIFY2(!u.startsWith(QStringLiteral("EvidenceText")), qPrintable(u));
}

QTEST_GUILESS_MAIN(EvidenceTextTest)
#include "test_evidence_text.moc"
