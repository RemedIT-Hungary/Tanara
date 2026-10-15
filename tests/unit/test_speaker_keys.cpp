// B2: language-neutral raw speaker keys ("speaker:2" instead of "Beszélő 2"). The key helpers, the read-side
// normalisation of old data ("Beszélő N" in tokens / segments / overlay / speakerMap / participants / summary
// snapshots), the transcript fingerprint that stays stable across the change, the display rendering in the
// UI language (hu source, en from the shipped i18n/tanara_en.ts) and the LLM rendering in the summary
// language, which the prompt's explanation shares.
#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/domain/SpeakerKeys.h"
#include "tanara/domain/SpeakerText.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/processing/summary/TranscriptMerger.h"
#include "ts_translator.h"

using namespace tanara;
using namespace tanara::speakerkeys;
using tanara::library::TranscriptRepository;

namespace {

using tstest::TsTranslator;

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

// A legacy meeting folder: tokens / segments / overlay written with the Hungarian labels.
void writeLegacyTranscript(const QString& folder)
{
    QJsonArray toks;
    toks.append(QJsonObject{{"text", " Szia"}, {"speaker", "Beszélő 1"}, {"startMs", 0}, {"endMs", 400}});
    toks.append(QJsonObject{{"text", " Helló"}, {"speaker", "Beszélő 2"}, {"startMs", 3000}, {"endMs", 3400}});
    writeFile(TranscriptRepository::tokensPath(folder), QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    writeFile(TranscriptRepository::segmentsPath(folder), QJsonDocument(QJsonArray{
        QJsonObject{{"startMs", 0}, {"endMs", 400}, {"speaker", "Beszélő 1"}, {"text", "Szia"}},
        QJsonObject{{"startMs", 3000}, {"endMs", 3400}, {"speaker", "Beszélő 2"}, {"text", "Helló"}}}).toJson());
}

} // namespace

class SpeakerKeysTest : public QObject {
    Q_OBJECT
private slots:
    void keyHelpers();
    void legacyNormalisation();
    void displayFollowsUiLanguage();
    void promptLabelFollowsSummaryLanguage();
    void repositoryNormalisesLegacyFiles();
    void fingerprintStableAcrossKeyChange();
    void speakerMapAndParticipantsNormalised();
};

void SpeakerKeysTest::keyHelpers()
{
    QCOMPARE(rawSpeakerKey(2), QStringLiteral("speaker:2"));
    QVERIFY(isRawSpeakerKey(QStringLiteral("speaker:2")));
    QVERIFY(!isRawSpeakerKey(QStringLiteral("speaker:")));
    QVERIFY(!isRawSpeakerKey(QStringLiteral("Beszélő 2")));
    QVERIFY(!isRawSpeakerKey(QStringLiteral("participant:1")));
    QCOMPARE(rawSpeakerIndex(QStringLiteral("speaker:7")), std::optional<int>(7));
    QCOMPARE(rawSpeakerIndex(QStringLiteral("Mikrofon 1")), std::optional<int>());
    QCOMPARE(rawSpeakerId(QStringLiteral("speaker:7")), QStringLiteral("7"));
    // Provider ids: a number, empty (no diarization), anything else verbatim.
    QCOMPARE(rawSpeakerKeyForId(QStringLiteral("3")), QStringLiteral("speaker:3"));
    QCOMPARE(rawSpeakerKeyForId(QString()), QStringLiteral("speaker:1"));
    QCOMPARE(rawSpeakerKeyForId(QStringLiteral("A")), QStringLiteral("speaker:A"));
}

void SpeakerKeysTest::legacyNormalisation()
{
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Beszélő 2")), QStringLiteral("speaker:2"));
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Beszélő")), QStringLiteral("speaker:1"));
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Beszélő 1@mic")), QStringLiteral("speaker:1@mic"));
    // Not a raw label: unchanged.
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("speaker:2")), QStringLiteral("speaker:2"));
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Beszélő Béla")), QStringLiteral("Beszélő Béla"));
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Mikrofon 1")), QStringLiteral("Mikrofon 1"));
    QCOMPARE(normalizeSpeakerLabel(QStringLiteral("Kovács Lilla")), QStringLiteral("Kovács Lilla"));
    QCOMPARE(normalizeSpeakerLabels({QStringLiteral("Beszélő 3"), QStringLiteral("Ádám")}),
             (QStringList{QStringLiteral("speaker:3"), QStringLiteral("Ádám")}));
    QCOMPARE(legacySpeakerLabel(QStringLiteral("speaker:4")), QStringLiteral("Beszélő 4"));
    QCOMPARE(legacySpeakerLabel(QStringLiteral("Ádám")), QStringLiteral("Ádám"));
}

void SpeakerKeysTest::displayFollowsUiLanguage()
{
    // Hungarian source.
    QCOMPARE(speakerLabelText(QStringLiteral("speaker:2")), QStringLiteral("Beszélő 2"));
    QCOMPARE(speakerLabelText(QStringLiteral("speaker:1@mic")), QStringLiteral("Beszélő 1@mic"));
    QCOMPARE(speakerLabelText(QStringLiteral("Kovács Lilla")), QStringLiteral("Kovács Lilla"));
    QCOMPARE(processing::participants::rawDisplay(QStringLiteral("speaker:3")), QStringLiteral("Beszélő 3"));
    // transcript.md: the raw key rendered, a name as is.
    const QString md = processing::renderUtterancesMarkdown({Utterance{0, 900, QStringLiteral("speaker:2"), QStringLiteral("Szia.")},
                                                             Utterance{1000, 1900, QStringLiteral("Ádám"), QStringLiteral("Helló.")}});
    QVERIFY2(md.contains(QStringLiteral("**Beszélő 2** Szia.")), qPrintable(md));
    QVERIFY(md.contains(QStringLiteral("**Ádám** Helló.")));
    QVERIFY(!md.contains(QStringLiteral("speaker:")));

    // English UI.
    TsTranslator en;
    QVERIFY(en.loadTs(QStringLiteral(TANARA_EN_TS)));
    QCoreApplication::installTranslator(&en);
    QCOMPARE(speakerLabelText(QStringLiteral("speaker:2")), QStringLiteral("Speaker 2"));
    QCOMPARE(speakerLabelText(QStringLiteral("Kovács Lilla")), QStringLiteral("Kovács Lilla"));
    QVERIFY(processing::renderUtterancesMarkdown({Utterance{0, 900, QStringLiteral("speaker:2"), QStringLiteral("Hi.")}})
                .contains(QStringLiteral("**Speaker 2** Hi.")));
    QCoreApplication::removeTranslator(&en);
    QCOMPARE(speakerLabelText(QStringLiteral("speaker:2")), QStringLiteral("Beszélő 2"));
}

void SpeakerKeysTest::promptLabelFollowsSummaryLanguage()
{
    using namespace tanara::processing;
    // The LLM's label: the summary language, not the UI language.
    TsTranslator en;
    QVERIFY(en.loadTs(QStringLiteral(TANARA_EN_TS)));
    QCoreApplication::installTranslator(&en);   // an English UI does not change a Hungarian summary's label
    QCOMPARE(promptSpeakerLabel(QStringLiteral("speaker:2"), QStringLiteral("magyar")), QStringLiteral("Beszélő 2"));
    QCOMPARE(promptSpeakerLabel(QStringLiteral("speaker:2"), QString()), QStringLiteral("Beszélő 2"));
    QCOMPARE(promptSpeakerLabel(QStringLiteral("speaker:2"), QStringLiteral("English")), QStringLiteral("Speaker 2"));
    QCOMPARE(promptSpeakerLabel(QStringLiteral("speaker:2"), QStringLiteral("német")), QStringLiteral("Speaker 2"));
    QCOMPARE(promptSpeakerLabel(QStringLiteral("Ádám"), QStringLiteral("English")), QStringLiteral("Ádám"));
    QCoreApplication::removeTranslator(&en);

    // The transcript the LLM gets and the prompt's explanation use the same label.
    for (const QString& lang : {QStringLiteral("magyar"), QStringLiteral("English")}) {
        MergedTranscript t;
        t.tokens = {TranscriptToken{QStringLiteral(" Szia."), QStringLiteral("speaker:2"), 0, 400, 1.0, QString()},
                    TranscriptToken{QStringLiteral(" Jó."), QStringLiteral("Ádám"), 3000, 3400, 1.0, QString()}};
        applyPromptSpeakerLabels(t, lang);
        const QString label = promptSpeakerLabel(rawSpeakerKey(2), lang);
        QVERIFY2(t.renderMarkdown().contains(QStringLiteral("**%1**").arg(label)), qPrintable(t.renderMarkdown()));
        QVERIFY(t.renderMarkdown().contains(QStringLiteral("**Ádám**")));
        for (const char* id : {"single", "notes", "merge", "topic", "analysis"}) {
            const QString prompt = applySummaryLanguage(promptBuiltin(QLatin1String(id)), lang);
            QVERIFY2(!prompt.contains(QStringLiteral("{{SPEAKER_LABEL}}")), id);
            if (promptBuiltin(QLatin1String(id)).contains(QStringLiteral("{{SPEAKER_LABEL}}")))
                QVERIFY2(prompt.contains(QStringLiteral("A label such as \"%1\" means the speaker was not identified").arg(label)), id);
        }
    }
    QVERIFY(promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("{{SPEAKER_LABEL}}")));
    QVERIFY(!promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("Beszélő")));
}

void SpeakerKeysTest::repositoryNormalisesLegacyFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeLegacyTranscript(dir.path());
    const MergedTranscript mt = TranscriptRepository::readTokens(dir.path());
    QCOMPARE(mt.tokens.size(), 2);
    QCOMPARE(mt.tokens.at(0).speaker, QStringLiteral("speaker:1"));
    QCOMPARE(mt.tokens.at(1).speaker, QStringLiteral("speaker:2"));
    const QVector<Utterance> segs = TranscriptRepository::readSegments(dir.path());
    QCOMPARE(segs.at(1).speaker, QStringLiteral("speaker:2"));
    const QVector<TranscriptLine> lines = TranscriptRepository::readLines(dir.path());
    QCOMPARE(lines.at(0).rawLabel, QStringLiteral("speaker:1"));

    // The overlay's raw-label references.
    writeFile(TranscriptRepository::overlayPath(dir.path()), QJsonDocument(QJsonObject{
        {"version", 3},
        {"merged", QJsonObject{{"Beszélő 2", "Beszélő 1"}}},
        {"removed", QJsonArray{"Beszélő 3"}},
        {"speakerTracks", QJsonObject{{"Beszélő 1", QJsonArray{"mic"}}}},
        {"utterances", QJsonObject{{"u0", QJsonObject{{"speaker", "Beszélő 2"}, {"corrected", true}}}}},
        {"identified", QJsonObject{{"Beszélő 1", QJsonObject{{"personId", PersonId::generate().toString()}, {"score", 0.8}}}}},
        {"summary", QJsonObject{{"changedSpeakers", QJsonArray{"Beszélő 2"}}}}}).toJson());
    const SpeakerOverlay ov = TranscriptRepository::readOverlay(dir.path());
    QCOMPARE(ov.merged.value(QStringLiteral("speaker:2")), QStringLiteral("speaker:1"));
    QCOMPARE(ov.removedRaw, QStringList{QStringLiteral("speaker:3")});
    QVERIFY(ov.speakerTracks.contains(QStringLiteral("speaker:1")));
    QCOMPARE(ov.utterances.value(QStringLiteral("u0")).speaker, QStringLiteral("speaker:2"));
    QVERIFY(ov.identified.contains(QStringLiteral("speaker:1")));
    QCOMPARE(ov.changedSinceSummary, QStringList{QStringLiteral("speaker:2")});

    // Written back: the neutral key (rewrites on the next save), read again: the same.
    QVERIFY(TranscriptRepository::writeTokens(dir.path(), mt));
    QCOMPARE(TranscriptRepository::readTokens(dir.path()).tokens.at(1).speaker, QStringLiteral("speaker:2"));
    QFile f(TranscriptRepository::tokensPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(!QString::fromUtf8(f.readAll()).contains(QStringLiteral("Beszélő")));
}

void SpeakerKeysTest::fingerprintStableAcrossKeyChange()
{
    // An overlay saved for the old ("Beszélő N") transcript still matches it after the read-side normalisation,
    // so the user's manual corrections survive.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeLegacyTranscript(dir.path());
    QVector<TranscriptLine> legacy = TranscriptRepository::readLines(dir.path());
    QVector<TranscriptLine> neutral = legacy;
    legacy[0].rawLabel = QStringLiteral("Beszélő 1");
    legacy[1].rawLabel = QStringLiteral("Beszélő 2");
    QCOMPARE(TranscriptRepository::linesFingerprint(neutral), TranscriptRepository::linesFingerprint(legacy));

    SpeakerOverlay ov;
    ov.transcriptFingerprint = TranscriptRepository::linesFingerprint(legacy);
    ov.merged.insert(QStringLiteral("speaker:2"), QStringLiteral("speaker:1"));
    QVERIFY(TranscriptRepository::writeOverlay(dir.path(), ov));
    const SpeakerOverlay back = TranscriptRepository::readOverlayFor(dir.path(), TranscriptRepository::readLines(dir.path()));
    QCOMPARE(back.merged.value(QStringLiteral("speaker:2")), QStringLiteral("speaker:1"));
}

void SpeakerKeysTest::speakerMapAndParticipantsNormalised()
{
    const PersonId who = PersonId::generate();
    Meeting m;
    m.id = QStringLiteral("m1");
    m.speakerMap.insert(QStringLiteral("speaker:2"), who);
    Participant p;
    p.id = QStringLiteral("v1");
    p.rawSpeakerIds = {QStringLiteral("speaker:2@loopback")};
    m.participants.append(p);
    QJsonObject o = library::toJson(m);
    // Rewrite as an old meeting.json: the Hungarian labels.
    o.insert(QStringLiteral("speakerMap"), QJsonObject{{QStringLiteral("Beszélő 2"), who.toString()},
                                                       {QStringLiteral("Mikrofon 1"), who.toString()}});
    QJsonArray parts = o.value(QStringLiteral("participants")).toArray();
    QJsonObject p0 = parts.at(0).toObject();
    p0.insert(QStringLiteral("rawSpeakerIds"), QJsonArray{QStringLiteral("Beszélő 2@loopback")});
    parts[0] = p0;
    o.insert(QStringLiteral("participants"), parts);

    const Meeting back = library::meetingFromJson(o);
    QCOMPARE(back.speakerMap.value(QStringLiteral("speaker:2")), who);
    QCOMPARE(back.speakerMap.value(QStringLiteral("Mikrofon 1")), who);   // a track label is not a raw speaker
    QVERIFY(!back.speakerMap.contains(QStringLiteral("Beszélő 2")));
    QCOMPARE(back.participants.at(0).rawSpeakerIds, QStringList{QStringLiteral("speaker:2@loopback")});
    // Round trip of the current form.
    QCOMPARE(library::meetingFromJson(library::toJson(back)).speakerMap, back.speakerMap);
}

QTEST_GUILESS_MAIN(SpeakerKeysTest)
#include "test_speaker_keys.moc"
