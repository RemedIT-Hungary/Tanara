#pragma once
//
// TranscriptDemoSession — az átirat-szerkesztő KITALÁLT mintaadata (App.demo, --qml-shot,
// --demo): egy ideiglenes mappában felépített meeting, rajta egy VALÓDI SpeakerEditor.
// Így a demó minden művelete (áthelyezés, összevonás, undo, javaslat, szűrő) ugyanazon a
// kódon fut, mint az éles — csak a „hang" hamis: a megszólalás-embedder a forgatókönyvből
// tudja, kinek a hangja szól. A felhasználó adataihoz (~/.tanara, ~/Tanara) nem nyúl.
//
// Változatok (TranscriptTab.demoVariant):
//   ""         4 beszélő, hosszú bekezdések, bizonytalan sorok, javaslatra alkalmas sorok
//   "two"      két beszélős interjú
//   "many"     11 beszélő (összecsukott „+N" oszlop, „Egyéb (N)" sor)
//   "long"     az első megszólalás nagyon hosszú monológ
//   "novoice"  nincs hangmodell (nincs bizonytalanság / javaslat / kézi lenyomat)
//   "none"     nincs átirat
//
#include "tanara/edit/SpeakerEditTypes.h"

#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <memory>

namespace tanara {
class MeetingStore;
class PeopleStore;
class VoiceprintStore;
class SpeakerEditor;
}

namespace tanara_qml {

class TranscriptDemoSession {
public:
    explicit TranscriptDemoSession(const QString& variant);
    ~TranscriptDemoSession();

    tanara::SpeakerEditor* editor() const { return m_editor.get(); }
    // A személyválasztók listája (kitalált nevek; a meeting résztvevői is benne vannak).
    QVector<tanara::PersonInfo> people() const;
    // A „javaslat" állapothoz: ezt a sort (nyersen „Távoli 1", hangra Fehér Ádám) kell
    // Fehér Ádámhoz áttenni. Üres, ha a változatban nincs ilyen.
    QString suggestionSeedUtteranceId() const { return m_seedId; }
    QString suggestionTargetKey() const { return m_seedTarget; }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<tanara::MeetingStore> m_store;
    std::unique_ptr<tanara::PeopleStore> m_people;
    std::unique_ptr<tanara::VoiceprintStore> m_prints;
    std::unique_ptr<tanara::SpeakerEditor> m_editor;
    QString m_seedId;
    QString m_seedTarget;
};

} // namespace tanara_qml
