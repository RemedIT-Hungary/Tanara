#pragma once
//
// The plan of the mixdown (mixdown.mp3): which track files go in, with what offset, and with what
// ffmpeg arguments. Pure (the only I/O is checking that the files exist), so the filter graph is
// testable; running it is the job of AppController::regenerateMixdown.
//
// Offset: a track that starts later (Track::startOffsetMs > 0: a device enabled during recording,
// the second span of a device, a track aligned with `align`) is put in place with adelay, so in the
// mix every sound is at its place by meeting time.
#include "tanara/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct MixdownInput {
    QString trackId;
    QString path;          // absolute path
    qint64  offsetMs = 0;  // goes into the mix shifted by this much silence
};

struct MixdownPlan {
    QVector<MixdownInput> inputs;   // the included (Track::included) tracks existing on disk (in meeting.json order)
    QStringList missing;            // display names of the tracks that are active but have a missing file

    // From the meeting's active tracks (dropped ones are left out).
    static MixdownPlan fromMeeting(const Meeting& m);

    // The mixing filter: empty for empty inputs. For one input without offset only the normalization
    // ("-af"), otherwise "-filter_complex" with adelays and amix. Returns the two arguments (switch + graph).
    static QStringList filterArgs(const QVector<qint64>& offsets);

    // The full ffmpeg argument list (without the program name): inputs, filter, MP3 encoding, progress
    // to stdout (-progress pipe:1), output. Empty inputs -> an empty list.
    QStringList ffmpegArgs(const QString& outPath) const;
};

} // namespace tanara
