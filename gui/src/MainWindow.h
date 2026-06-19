#pragma once
//
// MainWindow — Jamie-szerű fő ablak.
//   bal oldal: meeting-lista (QListView + MeetingListModel)
//   középen:   record bar + lapfül (Átirat / Összefoglaló)
//
#include "tanara/Types.h"
#include <QMainWindow>

class QTableView;
class QSortFilterProxyModel;
class QTextBrowser;
class QTabWidget;
class QPushButton;
class QProgressBar;
class QItemSelection;
class QVBoxLayout;
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
    void onIdentifyClicked();
    void onParticipantsClicked();
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

private:
    void buildUi();
    void buildMenu();
    void loadSelectedMeetingViews();
    void reloadMeetings();
    void setBusy(bool busy, const QString& msg = QString());
    tanara::Meeting selectedMeeting(bool* ok = nullptr) const;
    void reloadTranscriptView(const tanara::Meeting& m);
    void reloadSummaryView(const tanara::Meeting& m);
    static QString meetingAudioPath(const tanara::Meeting& m);
    static QString readMarkdownFile(const QString& path);

    Ui::MainWindow*         ui = nullptr;
    tanara::AppController*  m_controller = nullptr;
    MeetingTableModel*      m_tableModel = nullptr;
    QSortFilterProxyModel*  m_proxy = nullptr;

    QTableView*   m_table = nullptr;
    RecordBar*    m_recordBar = nullptr;
    QVBoxLayout*  m_rightLayout = nullptr;     // a jobb pane elrendezése (vissza-dokkoláshoz)
    QWidget*      m_dockPlaceholder = nullptr; // látszik, ha a vezérlő külön ablakban van
    FloatingRecorder* m_floatingRecorder = nullptr;
    QAction*      m_popOutAct = nullptr;       // Nézet → Felvétel külön ablakban
    QTabWidget*       m_tabs = nullptr;
    TranscriptPlayer* m_transcriptPlayer = nullptr;
    QTextBrowser*     m_summaryView = nullptr;
    TracksPanel*      m_tracksPanel = nullptr;

    QPushButton*  m_participantsBtn = nullptr;
    QPushButton*  m_transcribeBtn = nullptr;
    QPushButton*  m_summarizeBtn = nullptr;
    QPushButton*  m_identifyBtn = nullptr;
    QProgressBar* m_busyBar = nullptr;

    QString m_currentMeetingId;
    bool    m_monitoringStarted = false;
};

} // namespace tanara_gui
