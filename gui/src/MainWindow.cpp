#include "MainWindow.h"
#include "RecordBar.h"
#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingListModel.h"
#include "tanara/store/MeetingStore.h"

#include <QListView>
#include <QTextBrowser>
#include <QTabWidget>
#include <QPushButton>
#include <QSplitter>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QUrl>

#include <QMediaPlayer>
#include <QAudioOutput>

namespace tanara_gui {

MainWindow::MainWindow(tanara::AppController* controller, QWidget* parent)
    : QMainWindow(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Tanara"));
    resize(1100, 720);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    buildUi();
    buildMenu();

    // Modell összekötése a store-ral.
    m_model = new tanara::MeetingListModel(this);
    if (m_controller && m_controller->store())
        m_model->setStore(m_controller->store());
    m_list->setModel(m_model);

    if (auto* sel = m_list->selectionModel())
        connect(sel, &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onSelectionChanged);

    // Controller jelzések.
    if (m_controller) {
        connect(m_controller, &tanara::AppController::devicesChanged,
                m_recordBar, &RecordBar::onDevicesChanged);
        connect(m_controller, &tanara::AppController::recordingStateChanged,
                m_recordBar, &RecordBar::onRecordingStateChanged);
        connect(m_controller, &tanara::AppController::elapsedChanged,
                m_recordBar, &RecordBar::onElapsedChanged);
        connect(m_controller, &tanara::AppController::levelMeterUpdated,
                m_recordBar, &RecordBar::onLevelMeterUpdated);

        connect(m_controller, &tanara::AppController::transcriptReady,
                this, &MainWindow::onTranscriptReady);
        connect(m_controller, &tanara::AppController::summaryReady,
                this, &MainWindow::onSummaryReady);
        connect(m_controller, &tanara::AppController::errorOccurred,
                this, &MainWindow::onError);
        connect(m_controller, &tanara::AppController::recordingFinished,
                this, &MainWindow::onRecordingFinished);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // --- bal: meeting-lista ---
    m_list = new QListView(splitter);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setMinimumWidth(240);
    splitter->addWidget(m_list);

    // --- jobb: record bar + akciók + lapfülek ---
    auto* right = new QWidget(splitter);
    auto* rl = new QVBoxLayout(right);

    m_recordBar = new RecordBar(m_controller, right);
    rl->addWidget(m_recordBar);

    // akció-sor a kiválasztott meetingre
    auto* actionRow = new QHBoxLayout();
    m_transcribeBtn = new QPushButton(QStringLiteral("Átírás"), right);
    m_summarizeBtn = new QPushButton(QStringLiteral("Összefoglaló"), right);
    m_playBtn = new QPushButton(QStringLiteral("▶ Lejátszás"), right);
    actionRow->addWidget(m_transcribeBtn);
    actionRow->addWidget(m_summarizeBtn);
    actionRow->addWidget(m_playBtn);
    actionRow->addStretch(1);
    rl->addLayout(actionRow);

    // lapfülek
    m_tabs = new QTabWidget(right);
    m_transcriptView = new QTextBrowser(m_tabs);
    m_transcriptView->setOpenExternalLinks(true);
    m_summaryView = new QTextBrowser(m_tabs);
    m_summaryView->setOpenExternalLinks(true);
    m_tabs->addTab(m_transcriptView, QStringLiteral("Átirat"));
    m_tabs->addTab(m_summaryView, QStringLiteral("Összefoglaló"));
    rl->addWidget(m_tabs, 1);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    statusBar()->showMessage(QStringLiteral("Készen áll"));

    connect(m_transcribeBtn, &QPushButton::clicked, this, &MainWindow::onTranscribeClicked);
    connect(m_summarizeBtn, &QPushButton::clicked, this, &MainWindow::onSummarizeClicked);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayClicked);

    m_transcribeBtn->setEnabled(false);
    m_summarizeBtn->setEnabled(false);
    m_playBtn->setEnabled(false);
}

void MainWindow::buildMenu() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&Fájl"));
    QAction* settingsAct = fileMenu->addAction(QStringLiteral("Beállítások…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(QStringLiteral("Kilépés"));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);
}

tanara::Meeting MainWindow::selectedMeeting(bool* ok) const {
    if (ok) *ok = false;
    if (!m_list || !m_model)
        return {};
    const QModelIndex idx = m_list->currentIndex();
    if (!idx.isValid())
        return {};
    const QString id = m_model->data(idx, tanara::MeetingListModel::IdRole).toString();
    const QVector<tanara::Meeting>& all = m_model->meetings();
    for (const auto& m : all) {
        if (m.id == id) {
            if (ok) *ok = true;
            return m;
        }
    }
    return {};
}

QString MainWindow::readMarkdownFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    return ts.readAll();
}

void MainWindow::reloadTranscriptView(const tanara::Meeting& m) {
    const QString path = QDir(m.folder).filePath(QStringLiteral("transcript.md"));
    const QString md = readMarkdownFile(path);
    if (md.isEmpty())
        m_transcriptView->setPlainText(
            QStringLiteral("Nincs átirat. Indítsd el az „Átírás” műveletet."));
    else
        m_transcriptView->setMarkdown(md);
}

void MainWindow::reloadSummaryView(const tanara::Meeting& m) {
    const QString path = QDir(m.folder).filePath(QStringLiteral("summary.md"));
    const QString md = readMarkdownFile(path);
    if (md.isEmpty())
        m_summaryView->setPlainText(
            QStringLiteral("Nincs összefoglaló. Indítsd el az „Összefoglaló” műveletet."));
    else
        m_summaryView->setMarkdown(md);
}

void MainWindow::loadSelectedMeetingViews() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    const bool enable = ok;
    m_transcribeBtn->setEnabled(enable);
    m_summarizeBtn->setEnabled(enable);

    if (!ok) {
        m_currentMeetingId.clear();
        m_playBtn->setEnabled(false);
        m_transcriptView->clear();
        m_summaryView->clear();
        return;
    }

    m_currentMeetingId = m.id;
    reloadTranscriptView(m);
    reloadSummaryView(m);

    const QString mix = QDir(m.folder).filePath(
        m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
    m_playBtn->setEnabled(QFileInfo::exists(mix));
}

void MainWindow::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
    loadSelectedMeetingViews();
}

void MainWindow::onTranscribeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    statusBar()->showMessage(QStringLiteral("Átírás folyamatban…"));
    m_controller->transcribeMeeting(m.id);
}

void MainWindow::onSummarizeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    statusBar()->showMessage(QStringLiteral("Összefoglaló készítése…"));
    m_controller->summarizeMeeting(m.id);
}

void MainWindow::onPlayClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok)
        return;
    const QString mix = QDir(m.folder).filePath(
        m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
    if (!QFileInfo::exists(mix)) {
        statusBar()->showMessage(QStringLiteral("Nincs lejátszható hangfájl (mixdown.mp3)."));
        return;
    }

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        m_playBtn->setText(QStringLiteral("▶ Lejátszás"));
        return;
    }
    m_player->setSource(QUrl::fromLocalFile(mix));
    m_player->play();
    m_playBtn->setText(QStringLiteral("⏸ Szünet"));
}

void MainWindow::onTranscriptReady(QString meetingId, QString /*markdownPath*/) {
    statusBar()->showMessage(QStringLiteral("Átirat elkészült."), 5000);
    if (meetingId != m_currentMeetingId)
        return;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok) {
        reloadTranscriptView(m);
        m_tabs->setCurrentWidget(m_transcriptView);
    }
}

void MainWindow::onSummaryReady(QString meetingId, QString /*markdownPath*/) {
    statusBar()->showMessage(QStringLiteral("Összefoglaló elkészült."), 5000);
    if (meetingId != m_currentMeetingId)
        return;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok) {
        reloadSummaryView(m);
        m_tabs->setCurrentWidget(m_summaryView);
    }
}

void MainWindow::onError(QString message) {
    statusBar()->showMessage(QStringLiteral("Hiba: ") + message, 8000);
    QMessageBox::warning(this, QStringLiteral("Hiba"), message);
}

void MainWindow::onRecordingFinished(tanara::Meeting meeting) {
    statusBar()->showMessage(
        QStringLiteral("Felvétel kész: %1").arg(meeting.title), 5000);
    // A modell a store jelzéseire magától frissül; ettől függetlenül friss.
}

void MainWindow::openSettings() {
    SettingsDialog dlg(m_controller, this);
    dlg.exec();
}

} // namespace tanara_gui
