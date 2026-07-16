//
// Tanara — PwDumpParser unit-tesztek.
//
// A parser a `pw-dump` (PipeWire) JSON kimenetéből dönti el, folyik-e hívás:
// aktív (state=running) mikrofon-fogó stream (media.class=Stream/Input/Audio) egy
// ISMERT hívás-apptól, de NEM a saját binárisunktól. Tiszta függvény → befőzött
// JSON-fixture-ökkel teszteljük (nincs pw-dump futtatás).
//
// FONTOS: külön teszt-exe (lásd tests/CMakeLists.txt GLOB), saját main-nel. A core
// nem linkel Widgetset → QTEST_GUILESS_MAIN.
//
#include <QtTest>
#include <QObject>

#include "tanara/detect/detail/PwDumpParser.h"

using namespace tanara;

// Egyetlen Stream/Input/Audio node köré épített minimál pw-dump kimenet.
static QByteArray node(const QString& state, const QString& binary,
                       const QString& appName, const QString& mediaName)
{
    return QStringLiteral(R"([
      { "id": 10, "type": "PipeWire:Interface:Port", "info": { "props": {} } },
      { "id": 42, "type": "PipeWire:Interface:Node", "info": {
          "state": "%1",
          "props": {
            "media.class": "Stream/Input/Audio",
            "application.name": "%2",
            "application.process.binary": "%3",
            "media.name": "%4",
            "node.name": "%3"
          } } }
    ])").arg(state, appName, binary, mediaName).toUtf8();
}

class PwDumpParserTests : public QObject {
    Q_OBJECT
    const QStringList kKnown { QStringLiteral("zoom"), QStringLiteral("teams"),
                              QStringLiteral("discord"), QStringLiteral("firefox") };
    const QString kSelf { QStringLiteral("tanara") };

private slots:

    // Aktív (running) Zoom mikrofon-fogó → meeting; appId/appName kinyerve.
    void activeZoomIsMeeting()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "zoom", "ZOOM VoiceEngine", "recStream"), kKnown, kSelf);
        QVERIFY(s.active);
        QCOMPARE(s.appId, QStringLiteral("zoom"));
        QCOMPARE(s.appName, QStringLiteral("Zoom"));
    }

    // Ugyanaz a stream, de idle (nem fogja aktívan a mikrofont) → NEM meeting.
    void idleStreamIsNotMeeting()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("idle", "zoom", "ZOOM VoiceEngine", "recStream"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Ön-kizárás: a saját 'tanara' felvevő capture-je SOHA nem meeting.
    void selfCaptureExcluded()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "tanara", "tanara", "recStream"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Discord képernyőmegosztás/játék-hang (media.name=game capture) → NEM hívás.
    void gameCaptureIgnored()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "discord", "Discord", "game capture"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Ismeretlen (nem hívás-)app aktív capture-je, ami nincs a listán → NEM meeting.
    void unknownAppIgnored()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "audacity", "Audacity", "capture"),
            { QStringLiteral("zoom") }, kSelf);
        QVERIFY(!s.active);
    }

    // Playback (Stream/Output/Audio) stream nem mikrofon-fogás → figyelmen kívül.
    void playbackStreamIgnored()
    {
        const QByteArray json = QStringLiteral(R"([
          { "id": 7, "type": "PipeWire:Interface:Node", "info": {
              "state": "running",
              "props": { "media.class": "Stream/Output/Audio",
                         "application.process.binary": "zoom" } } }
        ])").toUtf8();
        const MeetingSignal s = detail::parsePwDump(json, kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Hibás/nem-tömb bemenet → {active:false}, nem dob, nem crashel.
    void garbageInputIsInactive()
    {
        QVERIFY(!detail::parsePwDump(QByteArray("not json"), kKnown, kSelf).active);
        QVERIFY(!detail::parsePwDump(QByteArray("{}"), kKnown, kSelf).active);
        QVERIFY(!detail::parsePwDump(QByteArray(), kKnown, kSelf).active);
    }
};

QTEST_GUILESS_MAIN(PwDumpParserTests)
#include "test_pwdump_parser.moc"
