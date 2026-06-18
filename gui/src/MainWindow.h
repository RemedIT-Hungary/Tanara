#pragma once
//
// MainWindow — Jamie-szerű fő ablak.
//   bal oldal: meeting-lista (QListView + MeetingListModel)
//   középen:   record bar + lapfül (Átirat / Összefoglaló)
//
#include "tanara/Types.h"
#include <QMainWindow>

class QListView;
class QTextBrowser;
class QTabWidget;
class QPushButton;
class QMediaPlayer;
class QAudioOutput;
class QItemSelection;

namespace tanara {
class AppController;
class MeetingListModel;
}

namespace tanara_gui {

class RecordBar;

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
    void onPlayClicked();
    void onTranscriptReady(QString meetingId, QString markdownPath);
    void onSummaryReady(QString meetingId, QString markdownPath);
    void onError(QString message);
    void openSettings();
    void onRecordingFinished(tanara::Meeting meeting);

private:
    void buildUi();
    void buildMenu();
    void loadSelectedMeetingViews();
    tanara::Meeting selectedMeeting(bool* ok = nullptr) const;
    void reloadTranscriptView(const tanara::Meeting& m);
    void reloadSummaryView(const tanara::Meeting& m);
    static QString readMarkdownFile(const QString& path);

    tanara::AppController*    m_controller = nullptr;
    tanara::MeetingListModel* m_model = nullptr;

    QListView*    m_list = nullptr;
    RecordBar*    m_recordBar = nullptr;
    QTabWidget*   m_tabs = nullptr;
    QTextBrowser* m_transcriptView = nullptr;
    QTextBrowser* m_summaryView = nullptr;

    QPushButton*  m_transcribeBtn = nullptr;
    QPushButton*  m_summarizeBtn = nullptr;
    QPushButton*  m_playBtn = nullptr;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    QString m_currentMeetingId;
    bool    m_monitoringStarted = false;
};

} // namespace tanara_gui
