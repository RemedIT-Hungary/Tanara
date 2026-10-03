#pragma once
//
// Tanara QML — KITALÁLT mintakönyvtár a héj demó- és képernyőkép-módjához (App.demo):
// nincs AppController, a könyvtár-modell, az „Ezek várnak rád” lista, a fejléc és a
// lejátszó ebből dolgozik. A nevek és szövegek fiktívek (a design-renderek M01/M02/M05
// mintái); valódi adat ide SOHA nem kerülhet.
//
#include "tanara/library/MeetingLibrary.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace tanara_qml::demo {

struct DemoMeeting {
    tanara::LibraryEntry entry;    // cím, idő, hossz, állapotok, résztvevők
    QString text;                  // egy mondatnyi „átirat” a keresés kivonatához
    int speakers = 0;              // beszélők száma (átirat után)
    int tracks = 0;                // hangsávok száma
};

// A demó „mai napja” (rögzített, hogy a dátum-szekciók és a képernyőképek stabilak legyenek).
QDateTime now();

const QVector<DemoMeeting>& meetings();
const DemoMeeting* find(const QString& meetingId);

// Ugyanaz a szerződés, mint a MeetingLibrary::query / people / pendingItems.
tanara::LibraryResult query(const tanara::LibraryQuery& q);
QVector<tanara::PersonPresence> people();
QVector<tanara::PendingItem> pendingItems();

// Az alapból megnyitott megbeszélés (van átirata és elavult összefoglalója).
QString defaultMeetingId();
// Átirat nélküli (bukott átírású) megbeszélés — a „preTranscript” állapot képéhez.
QString failedMeetingId();

} // namespace tanara_qml::demo
