// Tanara: PwDumpParser unit tests.
//
// From `pw-dump` (PipeWire) JSON output the parser decides whether a call is in progress: an
// active (state=running) microphone-capturing stream (media.class=Stream/Input/Audio) from a
// KNOWN call app, but NOT from our own binary. A pure function, so we test it with canned
// JSON fixtures (no pw-dump run).
//
// IMPORTANT: a separate test exe (see tests/CMakeLists.txt GLOB), with its own main. Core
// does not link Widgets, so QTEST_GUILESS_MAIN.
#include <QtTest>
#include <QObject>

#include "tanara/detect/detail/PwDumpParser.h"

using namespace tanara;

// A minimal pw-dump output built around a single Stream/Input/Audio node.
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

    // An active (running) Zoom microphone capture is a meeting; appId/appName extracted.
    void activeZoomIsMeeting()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "zoom", "ZOOM VoiceEngine", "recStream"), kKnown, kSelf);
        QVERIFY(s.active);
        QCOMPARE(s.appId, QStringLiteral("zoom"));
        QCOMPARE(s.appName, QStringLiteral("Zoom"));
    }

    // The same stream, but idle (not actively capturing the microphone): NOT a meeting.
    void idleStreamIsNotMeeting()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("idle", "zoom", "ZOOM VoiceEngine", "recStream"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Self-exclusion: our own 'tanara' recorder's capture is NEVER a meeting.
    void selfCaptureExcluded()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "tanara", "tanara", "recStream"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // Discord screen sharing / game audio (media.name=game capture): NOT a call.
    void gameCaptureIgnored()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "discord", "Discord", "game capture"), kKnown, kSelf);
        QVERIFY(!s.active);
    }

    // An active capture of an unknown (non-call) app that is not on the list: NOT a meeting.
    void unknownAppIgnored()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "audacity", "Audacity", "capture"),
            { QStringLiteral("zoom") }, kSelf);
        QVERIFY(!s.active);
    }

    // Google Meet in Vivaldi: the browser's "vivaldi-bin" process holds the microphone, the
    // media.name is just "RecordStream" (no "webrtc"). "meet" on the list covers every browser:
    // a meeting, named Vivaldi.
    void browserMeetViaMeetAlias()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "vivaldi-bin", "Vivaldi input", "RecordStream"),
            { QStringLiteral("zoom"), QStringLiteral("meet") }, kSelf);
        QVERIFY(s.active);
        QCOMPARE(s.appId, QStringLiteral("vivaldi"));
        QCOMPARE(s.appName, QStringLiteral("Vivaldi"));
    }

    // Without "meet" a browser's microphone use is not a call (not on the list).
    void browserWithoutMeetIgnored()
    {
        const MeetingSignal s = detail::parsePwDump(
            node("running", "vivaldi-bin", "Vivaldi input", "RecordStream"),
            { QStringLiteral("zoom") }, kSelf);
        QVERIFY(!s.active);
    }

    // A playback (Stream/Output/Audio) stream is not a microphone capture: ignored.
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

    // Broken / non-array input gives {active:false}, does not throw or crash.
    void garbageInputIsInactive()
    {
        QVERIFY(!detail::parsePwDump(QByteArray("not json"), kKnown, kSelf).active);
        QVERIFY(!detail::parsePwDump(QByteArray("{}"), kKnown, kSelf).active);
        QVERIFY(!detail::parsePwDump(QByteArray(), kKnown, kSelf).active);
    }
};

QTEST_GUILESS_MAIN(PwDumpParserTests)
#include "test_pwdump_parser.moc"
