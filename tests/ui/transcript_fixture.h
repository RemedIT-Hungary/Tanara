#pragma once
//
// Közös teszt-környezet az átirat-szerkesztő UI-tesztjeihez: ideiglenes mappában felépített,
// SZINTETIKUS meeting egy VALÓDI tanara::SpeakerEditor-ral és HAMIS embedderrel (a „hang" a
// megszólalás kezdőidejéből tudható). Valódi adatot, modellt vagy ffmpeg-et NEM használ.
//
// A forgatókönyv ugyanaz, mint a core tests/unit/test_speaker_editor.cpp-jében: a
// diarizáció két címkét adott („Beszélő 1", „Beszélő 2"), de a „Beszélő 1" valójában KÉT
// ember (A, C); „Beszélő 2" = B, egy sora (17.) tévesen került hozzá (A hangja).
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

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
        {19000,   800, kB2, 'B'},   // 3  rövid
        {22000,  4000, kB1, 'C'},   // 4
        {28000,  6000, kB2, 'B'},   // 5
        {36000,  4000, kB1, 'A'},   // 6
        {42000,  5000, kB1, 'C'},   // 7
        {49000,  4000, kB2, 'B'},   // 8
        {55000,  3500, kB1, 'C'},   // 9
        {60500,  4500, kB1, 'A'},   // 10
        {67000,  5000, kB2, 'B'},   // 11
        {74000,  1000, kB1, 'C'},   // 12 rövid
        {77000,  4000, kB1, 'A'},   // 13
        {83000,  6000, kB1, 'C'},   // 14
        {91000,  5000, kB2, 'B'},   // 15
        {98000,  4000, kB1, 'A'},   // 16
        {104000, 4000, kB2, 'A'},   // 17 tévesen a 2-es címkén
    };
    return rows;
}

inline QString uid(int row) { return QStringLiteral("u%1").arg(scenario()[row].startMs); }

class FakeEmbedder : public tanara::IUtteranceEmbedder {
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
    std::unique_ptr<tanara::MeetingStore> store;
    std::unique_ptr<tanara::PeopleStore> people;
    std::unique_ptr<tanara::VoiceprintStore> prints;
    tanara::Meeting meeting;

    Fixture()
    {
        store = std::make_unique<tanara::MeetingStore>(dir.filePath(QStringLiteral("rec")),
                                                       dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<tanara::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<tanara::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Teszt"));

        QJsonArray segs;
        int i = 0;
        for (const Row& r : scenario()) {
            QJsonObject o;
            o[QStringLiteral("startMs")] = double(r.startMs);
            o[QStringLiteral("endMs")] = double(r.startMs + r.durMs);
            o[QStringLiteral("speaker")] = r.raw;
            // A 7. sor szövege ékezetes, a kereső-teszthez.
            o[QStringLiteral("text")] = i == 7 ? QStringLiteral("Ödön szerint ez így jó lesz")
                                               : QStringLiteral("L%1a L%1b").arg(i);
            segs.append(o);
            ++i;
        }
        QFile f(tanara::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        store->saveMeeting(meeting);
    }

    void setSpeakerMap(const QMap<QString, QString>& map)
    {
        meeting = store->load(meeting.id);
        meeting.speakerMap = map;
        store->saveMeeting(meeting);
    }

    std::unique_ptr<tanara::SpeakerEditor> editor(bool withEmbedder = true)
    {
        auto ed = std::make_unique<tanara::SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        if (withEmbedder) ed->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
        return ed;
    }
};

} // namespace transcript_fixture
