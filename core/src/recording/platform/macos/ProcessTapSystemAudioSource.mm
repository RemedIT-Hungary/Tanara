// ISystemAudioSource on macOS 14.2+: a Core Audio process tap (docs/macos.md section 2). A global
// stereo tap of every process except Tanara sits in a PRIVATE aggregate device; that device is an
// ordinary input device for miniaudio in this process, so DeviceManager lists it among the captures
// and AudioEngine opens it like a microphone. Its name has the "Loopback:" prefix, so the common name
// rules (domain/AudioDeviceNames.h) classify it as system audio and the track is called "System audio".
//
// The tap and the device live as long as the process-wide instance (a private aggregate device also
// dies with the process). They cost nothing while no one records them: the audio flows, and the
// "System audio recording" permission is asked for, only when the device is started.
#include "tanara/recording/audio/SystemAudioSource.h"

#include "tanara/domain/AudioDeviceNames.h"
#include "tanara/foundation/log/Logging.h"

#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

#include <unistd.h>

namespace tanara::recording {

namespace {

// Tanara's own Core Audio process object (kAudioObjectUnknown if it has none yet).
AudioObjectID ownProcessObject()
{
    const AudioObjectPropertyAddress addr{kAudioHardwarePropertyTranslatePIDToProcessObject,
                                          kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    const pid_t pid = getpid();
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof(object);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, sizeof(pid), &pid, &size, &object);
    return object;
}

class ProcessTapSystemAudioSource final : public ISystemAudioSource {
public:
    ProcessTapSystemAudioSource()
    {
        if (@available(macOS 14.2, *))
            create();
        else
            qCWarning(lcAudio) << "Rendszerhang: a process tap macOS 14.2-t igényel.";
    }
    ~ProcessTapSystemAudioSource() override
    {
        if (aggregate_ != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(aggregate_);
        if (@available(macOS 14.2, *))
            if (tap_ != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(tap_);
    }

    QString id() const override { return QStringLiteral("coreaudio-tap"); }
    TrackKind classifyCapture(const QString& deviceName) const override
    {
        return audionames::classifyCaptureName(deviceName);   // the tap device: "Loopback: ..."
    }
    QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>&) const override
    {
        return {};   // the tap device is among the capture devices
    }
    bool opensFromPlayback(TrackKind) const override { return false; }

private:
    void create() API_AVAILABLE(macos(14.2))
    {
        @autoreleasepool {
            const AudioObjectID self = ownProcessObject();
            NSArray<NSNumber*>* excluded = self != kAudioObjectUnknown ? @[ @(self) ] : @[];
            CATapDescription* tap = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excluded];
            tap.name = @"Tanara";
            tap.privateTap = YES;
            OSStatus st = AudioHardwareCreateProcessTap(tap, &tap_);
            if (st != noErr) {
                tap_ = kAudioObjectUnknown;
                qCWarning(lcAudio) << "Rendszerhang: AudioHardwareCreateProcessTap hiba:" << st;
                return;
            }
            NSDictionary* desc = @{
                @kAudioAggregateDeviceNameKey: @"Loopback: System audio",
                @kAudioAggregateDeviceUIDKey: [@"eu.remedit.Tanara.tap." stringByAppendingString:tap.UUID.UUIDString],
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceTapAutoStartKey: @YES,
                @kAudioAggregateDeviceTapListKey: @[ @{@kAudioSubTapUIDKey: tap.UUID.UUIDString} ],
            };
            st = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)desc, &aggregate_);
            if (st != noErr) {
                aggregate_ = kAudioObjectUnknown;
                qCWarning(lcAudio) << "Rendszerhang: AudioHardwareCreateAggregateDevice hiba:" << st;
            }
        }
    }

    AudioObjectID tap_ = kAudioObjectUnknown;
    AudioObjectID aggregate_ = kAudioObjectUnknown;
};

} // namespace

std::unique_ptr<ISystemAudioSource> createSystemAudioSource()
{
    return std::make_unique<ProcessTapSystemAudioSource>();
}

} // namespace tanara::recording
