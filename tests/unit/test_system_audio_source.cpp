// Tanara: the system-audio port (recording::ISystemAudioSource) and the device-name rules behind it
// (domain/AudioDeviceNames.h), on fixed device lists. The platform's adapter is checked under #if.
#include <QtTest>
#include <QObject>

#include "tanara/domain/AudioDeviceNames.h"
#include "tanara/recording/audio/SystemAudioSource.h"

using namespace tanara;

class SystemAudioSourceTests : public QObject {
    Q_OBJECT
private slots:

    void nameRulesClassifyCaptureDevices()
    {
        // PulseAudio / PipeWire monitor sources: system audio.
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Monitor of Built-in Audio Analog Stereo")),
                 TrackKind::Loopback);
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Loopback: Kanto YU4")), TrackKind::Loopback);
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("MONITOR OF HDMI")), TrackKind::Loopback);
        // Line-in / AUX: Other (not a microphone).
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Built-in Audio Line-In")), TrackKind::Other);
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Vonalbemenet (Realtek)")), TrackKind::Other);
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Rear AUX")), TrackKind::Other);
        // Everything else: a microphone.
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Trust USB mikrofon")), TrackKind::Mic);
        QCOMPARE(audionames::classifyCaptureName(QStringLiteral("Mikrofon (Realtek(R) Audio)")), TrackKind::Mic);
    }

    void stripMonitorPrefix()
    {
        QCOMPARE(audionames::stripMonitorPrefix(QStringLiteral("Monitor of Kanto YU4 - Analog")),
                 QStringLiteral("Kanto YU4 - Analog"));
        QCOMPARE(audionames::stripMonitorPrefix(QStringLiteral("  loopback:  HDMI ")), QStringLiteral("HDMI"));
        QCOMPARE(audionames::stripMonitorPrefix(QStringLiteral("Monitor: X")), QStringLiteral("X"));
        QCOMPARE(audionames::stripMonitorPrefix(QStringLiteral("Trust USB mikrofon")),
                 QStringLiteral("Trust USB mikrofon"));
        // Only a prefix counts.
        QCOMPARE(audionames::stripMonitorPrefix(QStringLiteral("Studio Monitor of Doom")),
                 QStringLiteral("Studio Monitor of Doom"));
    }

    void processWideInstanceIsThePlatformOne()
    {
        QCOMPARE(recording::systemAudioSource().id(), recording::createSystemAudioSource()->id());
        QVERIFY(!recording::systemAudioSource().id().isEmpty());
    }

    void platformAdapter()
    {
        const std::unique_ptr<recording::ISystemAudioSource> src = recording::createSystemAudioSource();
        const QVector<recording::PlaybackEndpoint> outputs{
            {QStringLiteral("Speakers (Realtek Audio)"), true, 2},
            {QStringLiteral("HDMI Output"), false, 0},
        };
#if defined(Q_OS_LINUX)
        // The monitors are capture devices already: nothing derived from the outputs, and every kind
        // opens as a capture.
        QCOMPARE(src->id(), QStringLiteral("pulse-monitor"));
        const QStringList captures{QStringLiteral("Monitor of Built-in Audio Analog Stereo"),
                                   QStringLiteral("Built-in Audio Analog Stereo"),
                                   QStringLiteral("Built-in Audio Line-In")};
        const QVector<TrackKind> want{TrackKind::Loopback, TrackKind::Mic, TrackKind::Other};
        for (int i = 0; i < captures.size(); ++i)
            QCOMPARE(src->classifyCapture(captures.at(i)), want.at(i));
        QVERIFY(src->loopbackDevices(outputs).isEmpty());
        QVERIFY(!src->opensFromPlayback(TrackKind::Loopback));
        QVERIFY(!src->opensFromPlayback(TrackKind::Mic));
#elif defined(Q_OS_WIN)
        // WASAPI: one Loopback device per output, opened from the playback list.
        QCOMPARE(src->id(), QStringLiteral("wasapi-loopback"));
        const QVector<AudioDeviceInfo> lb = src->loopbackDevices(outputs);
        QCOMPARE(lb.size(), 2);
        QCOMPARE(lb.at(0).id, QStringLiteral("loopback:0:Speakers (Realtek Audio)"));
        QCOMPARE(lb.at(0).kind, TrackKind::Loopback);
        QVERIFY(lb.at(0).isDefault);
        QCOMPARE(lb.at(0).channels, 2);
        QCOMPARE(lb.at(1).id, QStringLiteral("loopback:1:HDMI Output"));
        QCOMPARE(lb.at(1).channels, 2);   // unknown native channel count: stereo
        QVERIFY(src->opensFromPlayback(TrackKind::Loopback));
        QVERIFY(!src->opensFromPlayback(TrackKind::Mic));
        QCOMPARE(src->classifyCapture(QStringLiteral("Mikrofon (USB)")), TrackKind::Mic);
#else
        QCOMPARE(src->id(), QStringLiteral("none"));
        QVERIFY(src->loopbackDevices(outputs).isEmpty());
        QVERIFY(!src->opensFromPlayback(TrackKind::Loopback));
#endif
    }
};

QTEST_GUILESS_MAIN(SystemAudioSourceTests)
#include "test_system_audio_source.moc"
