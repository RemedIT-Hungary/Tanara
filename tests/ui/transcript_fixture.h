#pragma once
// Shared test environment for the transcript editor's UI tests: a SYNTHETIC meeting built in
// a temporary folder with a REAL tanara::SpeakerEditor and a FAKE embedder (the "audio" is
// derived from the utterance's start time). It uses no real data, model or ffmpeg.
//
// The scenario is the same as in core's tests/unit/test_speaker_editor.cpp: diarization gave
// two labels ("Speaker 1", "Speaker 2"), but "Speaker 1" is really TWO people (A, C);
// "Speaker 2" = B, one of whose lines (17th) was wrongly attached (A's voice).
#include "tanara/domain/Types.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>

namespace transcript_fixture {

inline const QString kB1 = QStringLiteral("Beszélő 1");
inline const QString kB2 = QStringLiteral("Beszélő 2");

struct Row { qint64 startMs; qint64 durMs; QString raw; char voice; };

inline const QVector<Row>& scenario()
{
    static const QVector<Row> rows{
        {0,      4000, kB1, 'A'},   // 0
        {6000,   5000, kB2, 'B'},   // 1
        {13000,  4000, kB1, 'A'},   // 2
        {19000,   800, kB2, 'B'},   // 3  short
        {22000,  4000, kB1, 'C'},   // 4
        {28000,  6000, kB2, 'B'},   // 5
        {36000,  4000, kB1, 'A'},   // 6
        {42000,  5000, kB1, 'C'},   // 7
        {49000,  4000, kB2, 'B'},   // 8
        {55000,  3500, kB1, 'C'},   // 9
        {60500,  4500, kB1, 'A'},   // 10
        {67000,  5000, kB2, 'B'},   // 11
        {74000,  1000, kB1, 'C'},   // 12 short
        {77000,  4000, kB1, 'A'},   // 13
        {83000,  6000, kB1, 'C'},   // 14
        {91000,  5000, kB2, 'B'},   // 15
        {98000,  4000, kB1, 'A'},   // 16
        {104000, 4000, kB2, 'A'},   // 17 wrongly on label 2
    };
    return rows;
}

inline QString uid(int row) { return QStringLiteral("u%1").arg(scenario()[row].startMs); }

class FakeEmbedder : public tanara::processing::IUtteranceEmbedder {
public:
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        const qint64 mid = (startMs + endMs) / 2;
        for (const Row& r : scenario()) {
            if (mid < r.startMs || mid > r.startMs + r.durMs) continue;
            switch (r.voice) {
            case 'A': return {1.0f, 0.0f, 0.0f};
            case 'B': return {0.0f, 1.0f, 0.0f};
            default:  return {0.0f, 0.0f, 1.0f};
            }
        }
        return {};
    }
};

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<tanara::library::MeetingRepository> store;
    std::unique_ptr<tanara::library::PeopleStore> people;
    std::unique_ptr<tanara::library::VoiceprintStore> prints;
    tanara::Meeting meeting;

    Fixture()
    {
        store = std::make_unique<tanara::library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                                       dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<tanara::library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<tanara::library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Teszt"));

        QJsonArray segs;
        int i = 0;
        for (const Row& r : scenario()) {
            QJsonObject o;
            o[QStringLiteral("startMs")] = double(r.startMs);
            o[QStringLiteral("endMs")] = double(r.startMs + r.durMs);
            o[QStringLiteral("speaker")] = r.raw;
            // Line 7's text is accented, for the search test.
            o[QStringLiteral("text")] = i == 7 ? QStringLiteral("Ödön szerint ez így jó lesz")
                                               : QStringLiteral("L%1a L%1b").arg(i);
            segs.append(o);
            ++i;
        }
        QFile f(tanara::editing::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        store->saveMeeting(meeting);
    }

    // A person by name (people.json; created if missing).
    tanara::PersonId pid(const QString& name) { return people->ensure(name); }
    QString nameOf(const tanara::PersonId& id) const { return people->displayName(id); }

    // raw label -> person NAME (the people are created in people.json).
    void setSpeakerMap(const QMap<QString, QString>& map)
    {
        meeting = store->load(meeting.id);
        meeting.speakerMap.clear();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) meeting.speakerMap.insert(it.key(), pid(it.value()));
        store->saveMeeting(meeting);
    }

    std::unique_ptr<tanara::editing::SpeakerEditor> editor(bool withEmbedder = true)
    {
        auto ed = std::make_unique<tanara::editing::SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        if (withEmbedder) ed->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
        return ed;
    }
};

} // namespace transcript_fixture
