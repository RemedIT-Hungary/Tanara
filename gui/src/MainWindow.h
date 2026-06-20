#pragma once
//
// MainWindow — Jamie-szerű fő ablak.
//   bal oldal: meeting-lista (QListView + MeetingListModel)
//   középen:   record bar + lapfül (Átirat / Összefoglaló)
//
#include "tanara/Types.h"
#include <QMainWindow>
#include <QHash>
#include <QString>

class QTableView;
class QSortFilterProxyModel;
class QTextBrowser;
class QTabWidget;
class QStackedWidget;
class QPushButton;
class QToolButton;
class QLabel;
class QFrame;
class QProgressBar;
class QItemSelection;
class QVBoxLayout;
class QHBoxLayout;
class QAction;

namespace tanara {
class AppController;
}

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace tanara_gui {

class RecordBar;
class MeetingTableModel;
class TranscriptPlayer;
class FloatingRecorder;
class TracksPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(tanara::AppController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onTranscribeClicked();
    void onSummarizeClicked();
    // EGYETLEN „Résztvevők azonosítása" akció: átirat előtt előnézet (hang-klaszterek +
    // DB-találatok), átirat után a speakerMap kitöltése a biztos találatokkal — majd
    // emberi összegzés („3 különböző partner" / „Dompa, Béla és 1 ismeretlen partner").
    void onIdentifyParticipants();
    void onTranscriptReady(QString meetingId, QString markdownPath);
    void onSummaryReady(QString meetingId, QString markdownPath);
    void onError(QString message);
    void onJobProgress(QString meetingId, QString message);
    void onSpeakerMapChanged(QString meetingId);
    void openSettings();
    void openPeopleManager();
    void onRecordingFinished(tanara::Meeting meeting);
    void onTableContextMenu(const QPoint& pos);
    void renameSelectedMeeting();
    void deleteSelectedMeeting();
    void popOutRecorder();   // a felvétel-vezérlő külön (lebegő) ablakba
    void dockRecorder();     // vissza a főablakba
    void onTracksToggleClicked();      // a Sávok-fülre vált

private:
    void buildUi();
    void buildMenu();
    void loadSelectedMeetingViews();
    void reloadMeetings();
    void setBusy(bool busy, const QString& msg = QString());
    tanara::Meeting selectedMeeting(bool* ok = nullptr) const;
    void reloadTranscriptView(const tanara::Meeting& m);
    void reloadSummaryView(const tanara::Meeting& m);
    void reloadHeader(const tanara::Meeting& m);        // cím + meta (dátum · hossz)
    void reloadSpeakersBar(const tanara::Meeting& m);   // összecsukott beszélők-sáv
    void updateReviewGating(const tanara::Meeting& m);  // State A pipeline vs. fülek + kapuzás
    static QString humanDate(const tanara::Meeting& m);
    static QString durationHuman(qint64 ms);
    static QString meetingAudioPath(const tanara::Meeting& m);
    static QString readMarkdownFile(const QString& path);

    Ui::MainWindow*         ui = nullptr;
    tanara::AppController*  m_controller = nullptr;
    MeetingTableModel*      m_tableModel = nullptr;
    QSortFilterProxyModel*  m_proxy = nullptr;

    QTableView*   m_table = nullptr;
    // A felvevő ALAPBÓL leválasztott (pop-out): a RecordBar a FloatingRecorderben él,
    // a fő ablak jobb pane-jén nincs beágyazott recorder (Könyvtár-otthon mockup).
    RecordBar*    m_recordBar = nullptr;
    FloatingRecorder* m_floatingRecorder = nullptr;

    QTabWidget*       m_tabs = nullptr;
    QStackedWidget*   m_reviewStack = nullptr;   // page0 = fülek, page1 = State A pipeline
    TranscriptPlayer* m_transcriptPlayer = nullptr;
    QTextBrowser*     m_summaryView = nullptr;
    TracksPanel*      m_tracksPanel = nullptr;

    // --- felső sáv ---
    QPushButton*  m_newRecordingBtn = nullptr;
    QPushButton*  m_peopleBtn = nullptr;
    QPushButton*  m_settingsBtn = nullptr;

    // --- jobb pane fejléc + beszélők-sáv ---
    QLabel*       m_titleLabel = nullptr;
    QLabel*       m_metaLabel = nullptr;
    QToolButton*  m_tracksBtn = nullptr;
    QFrame*       m_speakersBar = nullptr;
    QLabel*       m_speakersSummary = nullptr;
    QPushButton*  m_speakersEditBtn = nullptr;

    // --- State A vezérelt pipeline-panel widgetjei ---
    // (A step1/step2box/step3/stepHint statikus címkék a .ui-ban élnek, kódból nem
    //  olvassuk/írjuk őket → nincs rájuk tag-pointer.)
    QLabel*       m_step2label = nullptr;
    QPushButton*  m_transcribeBtn = nullptr;   // a kapu-panel „Átírás indítása" gombja
    // State A: átirat ELŐTTI, hang-alapú résztvevő-tippelés belépője (nem kell átirat).
    QPushButton*  m_identifyParticipantsBtn = nullptr;
    QLabel*       m_participantsResult = nullptr;   // tartós eredmény-sor a State A panelben
    QHash<QString, QString> m_participantsCache;    // meetingId → utolsó eredmény-mondat (session)

    // --- az Összefoglaló-fül üres állapotának generálás-gombja (kódból injektálva) ---
    QPushButton*  m_generateSummaryBtn = nullptr;
    QWidget*      m_summaryEmptyPage = nullptr; // a summaryView helyén üres állapotban
    QStackedWidget* m_summaryStack = nullptr;   // page0 = summaryView, page1 = üres+gomb

    QProgressBar* m_busyBar = nullptr;

    QString m_currentMeetingId;
    bool    m_monitoringStarted = false;
};

} // namespace tanara_gui
