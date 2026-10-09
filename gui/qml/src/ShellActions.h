#pragma once
//
// ShellActions — a héj művelet-felülete (gui/qml/CONTRACT.md): minden, amihez Widgets-ablak,
// cloud-becslés / bejelentkezés / hibaág, vagy az ablak tartalmának váltása kell.
//
// A tartalom-komponensek `shell` property-ként kapják, és SOHA nem hívják közvetlenül az
// AppController feldolgozás-indítóit: itt fut le a kapuzás —
//   canRun (ReadinessModel) → cloud-akadály (bejelentkezés / feltöltés / frissítés) →
//   Beállítások a megfelelő lapon → K-06 becslés-megerősítés → indítás.
// A Widgets-t igénylő lépések a ShellBridge-en (App.bridge) mennek át; a QML-ben élő
// párbeszédablakokat (M10 minta) és az értesítő sávot jelekkel kéri a Main.qml-től.
//
#include "PlayerController.h"

#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"
#include "tanara/provider/ReadinessModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class ShellBridge;

class ShellActions : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // A kijelölt megbeszélés ("" = nincs) és az aktív fül (0 átirat, 1 összefoglaló, 2 sávok).
    Q_PROPERTY(QString currentMeetingId READ currentMeetingId WRITE setCurrentMeetingId
               NOTIFY currentMeetingIdChanged)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    // A Main.qml lejátszója (a seekTo ezt tekeri).
    Q_PROPERTY(tanara_qml::PlayerController* player READ player WRITE setPlayer NOTIFY playerChanged)
    // Fut-e felvétel (a bezárás-védelemhez). Controller nélkül mindig hamis.
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    // Növekszik, ha a lépések futtathatósága megváltozhatott (beállítások, cloud be-/kilépés):
    // a nézetek kötésbe véve újraértékelhetik a canRun-t.
    Q_PROPERTY(int readinessRevision READ readinessRevision NOTIFY readinessRevisionChanged)

public:
    explicit ShellActions(QObject* parent = nullptr);

    // Alapból az App-singleton controllere / hídja; tesztben injektálható.
    void setController(tanara::AppController* controller);
    void setBridge(ShellBridge* bridge);

    QString currentMeetingId() const { return m_currentMeetingId; }
    void setCurrentMeetingId(const QString& id);
    int currentTab() const { return m_currentTab; }
    void setCurrentTab(int index);
    PlayerController* player() const { return m_player; }
    void setPlayer(PlayerController* player);
    bool recording() const;
    int readinessRevision() const { return m_readinessRevision; }

    // ---- a szerződés műveletei ----
    // page: "" | "providers" | "watcher" | "cloud" | "summary" | "recording";
    // focusField: "" | "stt" | "llm" — a hiányzó szolgáltatóhoz vezető mély hivatkozás (B04).
    Q_INVOKABLE void openSettings(const QString& page = QString(),
                                  const QString& focusField = QString());
    // person: ez a személy legyen kijelölve a Személyek ablakban (üres: nincs kérés).
    Q_INVOKABLE void openPeople(const QString& person = QString());
    // A Címkék ablaka; tagId: ez a címke legyen kijelölve (üres: nincs kérés).
    Q_INVOKABLE void openTags(const QString& tagId = QString());
    // Az „Első lépések” ablak (Fájl › „Első lépések…”).
    Q_INVOKABLE void openOnboarding();
    Q_INVOKABLE void openRecorder();
    Q_INVOKABLE void startTranscription(const QString& meetingId);
    Q_INVOKABLE void retranscribe(const QString& meetingId);
    Q_INVOKABLE void startQuickSummary(const QString& meetingId);
    Q_INVOKABLE void startTopicExtraction(const QString& meetingId);
    Q_INVOKABLE void startTopicAnalysis(const QString& meetingId);
    Q_INVOKABLE void analyzeTopic(const QString& meetingId, const QString& topicId);
    Q_INVOKABLE void identifyParticipants(const QString& meetingId);
    // Újraellenőrzés a megerősített / javított sorok hangja alapján (fejléc „…” menü, az átirat
    // „Bizonytalan 0” gombja): megerősítő ablak → SpeakerEditor::recheckFromConfirmed →
    // visszajelzés; ha nem futtatható, az okát mondja el. Az „Résztvevők azonosítása” is ide
    // jut, ha már minden beszélőnek van neve.
    Q_INVOKABLE void recheckSpeakers(const QString& meetingId);
    Q_INVOKABLE void cancelJob(const QString& meetingId, int jobKind);
    // „Betöltés nagyobb kontextussal” (LM Studio): a következő LLM-feladat előtt a modell
    // legalább ennyi tokenes kontextussal töltődik újra. A feladatot a hívó indítja újra.
    Q_INVOKABLE void requestLlmContext(int tokens);
    Q_INVOKABLE void revealInFolder(const QString& meetingId);
    Q_INVOKABLE QString pickAudioFile();
    Q_INVOKABLE QStringList pickAudioFiles();
    // „Hangfájl importálása…”: fájlok nélkül előbb a natív választó nyílik (visszalépésre nem
    // történik semmi); a megadott (pl. az ablakra ejtett) fájlokkal rögtön a párbeszédablak.
    // Futó importálás mellett a haladását mutató ablak jön elő.
    Q_INVOKABLE void openImport(const QVariantList& files = {});
    // Megbeszélés-archívum (*.tanara.zip, lásd store/MeetingArchive.h).
    // „Exportálás archívumba…”: natív mentés-ablak (alapból „<mappanév>.tanara.zip” a legutóbb
    // használt mappában, ill. ~/Tanara-ban), majd háttérben fut — a haladás a feladat-sávban,
    // a végén toast „Megnyitás mappában” gombbal.
    Q_INVOKABLE void exportArchive(const QString& meetingId);
    // „Megbeszélés importálása archívumból…”: üres úttal előbb a natív választó; a végén az új
    // megbeszélés kijelölődik + toast; hiba toastban. (Ráejtett .zip is ide jut: fájl-URL is jó.)
    Q_INVOKABLE void importArchive(const QVariant& pathOrUrl = QVariant());
    // „Megbeszélés importálása mappából…”: egy kimásolt felvétel-mappa (zip nélkül); üres úttal
    // natív mappaválasztó. A vége ugyanaz, mint az archívumnál (kijelölés + toast / hiba toast).
    Q_INVOKABLE void importFolder(const QVariant& pathOrUrl = QVariant());
    // Archívumnak látszik-e (a ráejtett fájlok szétválogatásához): *.zip.
    Q_INVOKABLE bool isArchiveFile(const QVariant& pathOrUrl) const;
    // Mappa-e (a ráejtett elemek szétválogatásához): egy ráejtett megbeszélés-mappa importálható.
    Q_INVOKABLE bool isDirectory(const QVariant& pathOrUrl) const;
    // Egy fájl mappájának megnyitása a fájlkezelőben (a toast „Megnyitás mappában” gombja).
    Q_INVOKABLE void revealFile(const QString& path);
    Q_INVOKABLE bool confirm(const QString& title, const QString& text,
                             const QString& confirmLabel, bool danger);
    Q_INVOKABLE void showMeeting(const QString& meetingId);
    Q_INVOKABLE void showTab(int index);
    Q_INVOKABLE void seekTo(const QString& meetingId, int ms);
    // undoKey: nem üres → a toaston „Visszavonás” gomb, amely undoFromToast(undoKey)-t hív
    // (a kulcs gazdája — pl. "tags": a címke-lépések — az undoRequested jelre visszavon).
    Q_INVOKABLE void toast(const QString& text, const QString& undoKey = QString());

    // ---- a héj saját műveletei (a szerződésen túl) ----
    // Az újra-átírás megerősítő ablakának adatai: { title, text, corrections, any }.
    Q_INVOKABLE QVariantMap retranscribeImpact(const QString& meetingId) const;
    // A megerősítő ablak „Újra-átírás” gombja: kapuzás + becslés + indítás.
    Q_INVOKABLE void confirmRetranscribe(const QString& meetingId, bool keepBackup);
    // Törlés: megerősítő ablakot kér (deleteDialogRequested); a megerősítés után deleteMeeting.
    Q_INVOKABLE void requestDelete(const QString& meetingId);
    Q_INVOKABLE void deleteMeeting(const QString& meetingId);
    Q_INVOKABLE void renameMeeting(const QString& meetingId, const QString& title);
    // A fejléc címének helyben szerkesztése (a könyvtár helyi menüjéből is).
    Q_INVOKABLE void requestRename(const QString& meetingId);
    Q_INVOKABLE void stopRecording();
    // A főablak előtérbe hozása (pl. a felvevő „Megnyitás az elemzőben” gombja:
    // showMeeting(id) + activateWindow()).
    Q_INVOKABLE void activateWindow();
    // A confirm() QML-ablakának válasza.
    Q_INVOKABLE void resolveConfirm(bool accepted);
    // Az átirat előtti (hang-alapú) résztvevő-tipp utolsó eredménye erre a megbeszélésre.
    Q_INVOKABLE QString participantsGuess(const QString& meetingId) const;
    // A toast „Visszavonás” gombja (és a Ctrl+Z a címkesor fókuszában): undoRequested(undoKey).
    Q_INVOKABLE void undoFromToast(const QString& undoKey);
    // A könyvtár szűrése egy címkére (a fejléc chipje, a Címkék ablaka): tagFilterRequested.
    Q_INVOKABLE void filterByTag(const QString& tagId);
    // Van-e a megbeszélésnek átirata / létezik-e (a QML gyors kérdései).
    Q_INVOKABLE bool meetingExists(const QString& meetingId) const;

signals:
    void currentMeetingIdChanged();
    void currentTabChanged();
    void playerChanged();
    void recordingChanged();
    void readinessRevisionChanged();

    // Az Editor a megadott időpontú megszólaláshoz görget (szerződés).
    void transcriptPositionRequested(int ms);

    // ---- a Main.qml-nek ----
    // tone: "" (semleges) | "danger"; requestId / usageLink: cloud-értesítéseknél.
    // undoKey: lásd toast(). revealPath: nem üres → „Megnyitás mappában” gomb (revealFile).
    void toastRequested(const QString& text, const QString& tone, const QString& requestId,
                        bool usageLink, const QString& undoKey = QString(),
                        const QString& revealPath = QString());
    void undoRequested(const QString& undoKey);
    // A Main.qml a könyvtár-modellre teszi a címke-szűrőt.
    void tagFilterRequested(const QString& tagId);
    void confirmRequested(const QString& title, const QString& text,
                          const QString& confirmLabel, bool danger);
    void retranscribeDialogRequested(const QString& meetingId);
    void deleteDialogRequested(const QString& meetingId, const QString& title);
    void renameRequested(const QString& meetingId);
    void importDialogRequested(const QVariantList& files);
    void windowActivationRequested();
    // Az átirat előtti résztvevő-tipp elkészült (PreTranscriptView megjelenítheti).
    void participantsGuessed(const QString& meetingId, const QString& summary);

private:
    void attachController();
    void attachBridge();
    ShellBridge* bridge() const;
    tanara::Meeting meeting(const QString& meetingId) const;
    // A lépés futtathatósága; ha nem megy, a megfelelő teendőhöz visz. true = indítható.
    bool gate(tanara::WorkflowStep step, const QString& meetingId);
    bool estimateOk(const QString& meetingId, const QString& task, const QString& mode);
    void bumpReadiness();
    void onRetry(const QString& meetingId, const QString& kind);
    void onJobFinished(const QString& meetingId, tanara::JobKind kind, tanara::JobOutcome outcome);
    QString speakerSummary(const QString& meetingId) const;
    void runRecheck(const QString& meetingId, const QString& title, const QString& text);

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_attachedController;
    QPointer<ShellBridge> m_bridge;
    QPointer<ShellBridge> m_attachedBridge;
    bool m_controllerInjected = false;
    bool m_bridgeInjected = false;
    QPointer<PlayerController> m_player;

    QString m_currentMeetingId;
    int m_currentTab = 0;
    int m_readinessRevision = 0;
    QSet<QString> m_identifyRequested;              // a felhasználó kérte az azonosítást
    QHash<QString, QString> m_participantGuesses;   // meetingId → összegző mondat (munkamenet)
    // Az épp most rögzített, a megnyitott megbeszélés hibasávjában LÁTHATÓ feladat-hiba
    // üzenete (ugyanabban az esemény-körben érkező errorOccurred-ből nem lesz második toast).
    QString m_errorInBanner;
    QString m_lastArchiveDir;   // az utolsó export célmappája (munkameneten belül)

    // confirm(): beágyazott eseményhurok, amíg a QML-ablak válaszol.
    class QEventLoop* m_confirmLoop = nullptr;
    bool m_confirmResult = false;
};

} // namespace tanara_qml
