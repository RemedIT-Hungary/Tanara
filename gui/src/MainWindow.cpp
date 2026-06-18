#include "MainWindow.h"
#include "RecordBar.h"
#include "SettingsDialog.h"
#include "MeetingTableModel.h"
#include "TranscriptPlayer.h"
#include "PeopleManagerDialog.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

#include <QTableView>
#include <QHeaderView>
#include <QSortFilterProxyModel>
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
#include <QProgressBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QPoint>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QUrl>

#include <QShowEvent>
#include <QCloseEvent>

namespace tanara_gui {

MainWindow::MainWindow(tanara::AppController* controller, QWidget* parent)
    : QMainWindow(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Tanara"));
    resize(1100, 720);

    // A QMediaPlayer-t LUSTÁN hozzuk létre (első lejátszáskor), hogy indításkor ne
    // triggerelje a Qt Multimedia videó-hwaccel próbáját (libvdpau stderr-zaj).
    buildUi();
    buildMenu();

    // GUI-oldali táblamodell + rendező proxy.
    m_tableModel = new MeetingTableModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_tableModel);
    m_proxy->setSortRole(Qt::EditRole);   // típushelyes rendezés (lásd MeetingTableModel)
    m_proxy->setDynamicSortFilter(true);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(MeetingTableModel::ColTime, Qt::DescendingOrder);

    // Oszlopszélességek (a Név oszlop nyúlik — setStretchLastSection a buildUi-ban).
    m_table->setColumnWidth(MeetingTableModel::ColTime, 130);
    m_table->setColumnWidth(MeetingTableModel::ColDuration, 60);

    reloadMeetings();

    if (auto* sel = m_table->selectionModel())
        connect(sel, &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onSelectionChanged);

    // Store jelzésekre frissítjük a táblát.
    if (m_controller && m_controller->store()) {
        connect(m_controller->store(), &tanara::MeetingStore::meetingAdded,
                this, [this](const QString&) { reloadMeetings(); });
        connect(m_controller->store(), &tanara::MeetingStore::meetingUpdated,
                this, [this](const QString&) { reloadMeetings(); });
    }

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
        connect(m_controller, &tanara::AppController::deviceLevel,
                m_recordBar, &RecordBar::onDeviceLevel);

        connect(m_controller, &tanara::AppController::transcriptReady,
                this, &MainWindow::onTranscriptReady);
        connect(m_controller, &tanara::AppController::summaryReady,
                this, &MainWindow::onSummaryReady);
        connect(m_controller, &tanara::AppController::errorOccurred,
                this, &MainWindow::onError);
        connect(m_controller, &tanara::AppController::jobProgress,
                this, &MainWindow::onJobProgress);
        connect(m_controller, &tanara::AppController::recordingFinished,
                this, &MainWindow::onRecordingFinished);
        connect(m_controller, &tanara::AppController::speakerMapChanged,
                this, &MainWindow::onSpeakerMapChanged);

        // A táblát ezek a jelzések is frissítik (új meeting / friss átirat-jelölés).
        connect(m_controller, &tanara::AppController::recordingFinished,
                this, [this](const tanara::Meeting&) { reloadMeetings(); });
        connect(m_controller, &tanara::AppController::transcriptReady,
                this, [this](const QString&, const QString&) { reloadMeetings(); });
        connect(m_controller, &tanara::AppController::summaryReady,
                this, [this](const QString&, const QString&) { reloadMeetings(); });
    }
}

MainWindow::~MainWindow() {
    // Biztosítjuk, hogy a capture-eszközök elengedésre kerüljenek kilépéskor.
    if (m_controller)
        m_controller->stopLevelMonitoring();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // Ablak megjelenésekor — ha épp NEM veszünk fel — indítjuk az élő
    // szintfigyelést, hogy a VU-sávok mozogjanak és az eszközök azonosíthatók
    // legyenek. Csak egyszer (a recordingStateChanged kezeli az újraindítást).
    if (!m_monitoringStarted && m_controller
        && m_controller->recordingState() == tanara::RecordingState::Idle) {
        m_controller->startLevelMonitoring();
        m_monitoringStarted = true;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_controller)
        m_controller->stopLevelMonitoring();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // --- bal: meeting-lista (rendezhető tábla) ---
    m_table = new QTableView(splitter);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setMinimumWidth(320);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTableContextMenu);
    splitter->addWidget(m_table);

    // --- jobb: record bar + akciók + lapfülek ---
    auto* right = new QWidget(splitter);
    auto* rl = new QVBoxLayout(right);

    m_recordBar = new RecordBar(m_controller, right);
    rl->addWidget(m_recordBar);

    // akció-sor a kiválasztott meetingre (a lejátszást már a TranscriptPlayer
    // sávja kezeli az Átirat fülön, külön Lejátszás-gomb nincs).
    auto* actionRow = new QHBoxLayout();
    m_transcribeBtn = new QPushButton(QStringLiteral("Átírás"), right);
    m_summarizeBtn = new QPushButton(QStringLiteral("Összefoglaló"), right);
    actionRow->addWidget(m_transcribeBtn);
    actionRow->addWidget(m_summarizeBtn);
    actionRow->addStretch(1);
    rl->addLayout(actionRow);

    // lapfülek
    m_tabs = new QTabWidget(right);
    m_transcriptPlayer = new TranscriptPlayer(m_tabs);
    m_transcriptPlayer->setController(m_controller);
    m_summaryView = new QTextBrowser(m_tabs);
    m_summaryView->setOpenExternalLinks(true);
    m_tabs->addTab(m_transcriptPlayer, QStringLiteral("Átirat"));
    m_tabs->addTab(m_summaryView, QStringLiteral("Összefoglaló"));
    rl->addWidget(m_tabs, 1);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    statusBar()->showMessage(QStringLiteral("Készen áll"));
    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);            // indeterminált (pörgő) busy-jelző
    m_busyBar->setMaximumWidth(160);
    m_busyBar->setTextVisible(false);
    m_busyBar->setVisible(false);
    statusBar()->addPermanentWidget(m_busyBar);

    connect(m_transcribeBtn, &QPushButton::clicked, this, &MainWindow::onTranscribeClicked);
    connect(m_summarizeBtn, &QPushButton::clicked, this, &MainWindow::onSummarizeClicked);

    m_transcribeBtn->setEnabled(false);
    m_summarizeBtn->setEnabled(false);
}

void MainWindow::buildMenu() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&Fájl"));
    QAction* settingsAct = fileMenu->addAction(QStringLiteral("Beállítások…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    QAction* peopleAct = fileMenu->addAction(QStringLiteral("Személyek…"));
    connect(peopleAct, &QAction::triggered, this, &MainWindow::openPeopleManager);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(QStringLiteral("Kilépés"));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::reloadMeetings() {
    if (!m_tableModel || !m_controller || !m_controller->store())
        return;
    // A kijelölt meeting megőrzése id szerint (a reset után visszaállítjuk).
    const QString keepId = m_currentMeetingId;

    m_tableModel->setMeetings(m_controller->store()->loadAll());

    if (keepId.isEmpty() || !m_table->selectionModel())
        return;
    for (int row = 0; row < m_tableModel->rowCount(); ++row) {
        if (m_tableModel->idAt(row) == keepId) {
            const QModelIndex proxyIdx =
                m_proxy->mapFromSource(m_tableModel->index(row, 0));
            if (proxyIdx.isValid())
                m_table->selectionModel()->setCurrentIndex(
                    proxyIdx, QItemSelectionModel::ClearAndSelect
                                  | QItemSelectionModel::Rows);
            break;
        }
    }
}

tanara::Meeting MainWindow::selectedMeeting(bool* ok) const {
    if (ok) *ok = false;
    if (!m_table || !m_proxy || !m_tableModel)
        return {};
    const QModelIndex proxyIdx = m_table->currentIndex();
    if (!proxyIdx.isValid())
        return {};
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    if (!srcIdx.isValid())
        return {};
    if (ok) *ok = true;
    const tanara::Meeting indexMeeting = m_tableModel->meetingAt(srcIdx.row());
    // A tábla az SQLite-INDEXből jön (nincs benne speakerMap/tracks). A részletekhez
    // a TELJES meetinget LEMEZRŐL (meeting.json) töltjük, hogy a beszélő-nevek és a
    // sávok is meglegyenek.
    if (m_controller && m_controller->store() && !indexMeeting.id.isEmpty()) {
        const tanara::Meeting full = m_controller->store()->load(indexMeeting.id);
        if (!full.id.isEmpty())
            return full;
    }
    return indexMeeting;
}

QString MainWindow::readMarkdownFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    return ts.readAll();
}

QString MainWindow::meetingAudioPath(const tanara::Meeting& m) {
    return QDir(m.folder).filePath(
        m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
}

void MainWindow::reloadTranscriptView(const tanara::Meeting& m) {
    // Az Átirat fül most a TranscriptPlayer: a segments.json-t és a hangforrást
    // töltjük be (auto-play NÉLKÜL). Hiányzó segments.json esetén a widget
    // maga jeleníti meg a „Nincs átirat — futtass Átírást.” placeholdert.
    m_transcriptPlayer->loadMeeting(m, meetingAudioPath(m));
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
        m_transcriptPlayer->clearMeeting();
        m_summaryView->clear();
        return;
    }

    m_currentMeetingId = m.id;
    reloadTranscriptView(m);   // betölti a segments.json-t + hangforrást a lejátszóba
    reloadSummaryView(m);
}

void MainWindow::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
    loadSelectedMeetingViews();
}

void MainWindow::onTranscribeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    setBusy(true, QStringLiteral("Átírás indítása…"));
    m_controller->transcribeMeeting(m.id);
}

void MainWindow::onSummarizeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    setBusy(true, QStringLiteral("Összefoglaló készítése…"));
    m_controller->summarizeMeeting(m.id);
}

void MainWindow::onTranscriptReady(QString meetingId, QString /*markdownPath*/) {
    setBusy(false);
    statusBar()->showMessage(QStringLiteral("Átirat elkészült."), 5000);
    if (meetingId != m_currentMeetingId)
        return;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok) {
        reloadTranscriptView(m);
        m_tabs->setCurrentWidget(m_transcriptPlayer);
    }
}

void MainWindow::onSummaryReady(QString meetingId, QString /*markdownPath*/) {
    setBusy(false);
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
    setBusy(false);
    statusBar()->showMessage(QStringLiteral("Hiba: ") + message, 8000);
    QMessageBox::warning(this, QStringLiteral("Hiba"), message);
}

void MainWindow::onJobProgress(QString /*meetingId*/, QString message) {
    setBusy(true, message);
}

void MainWindow::onSpeakerMapChanged(QString meetingId) {
    // Beszélő-átnevezés után: a store frissült (people.json + meeting.json), így a
    // tábla újratöltése friss speakerMap-et ad. Ha az érintett meeting az épp
    // megjelenített, újrarendereljük az Átirat-nézetet (sorok + beszélők-panel).
    reloadMeetings();
    if (meetingId != m_currentMeetingId)
        return;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok)
        reloadTranscriptView(m);
}

void MainWindow::setBusy(bool busy, const QString& msg) {
    if (m_busyBar) m_busyBar->setVisible(busy);
    if (!msg.isEmpty()) statusBar()->showMessage(msg);
    if (busy) {
        if (m_transcribeBtn) m_transcribeBtn->setEnabled(false);
        if (m_summarizeBtn)  m_summarizeBtn->setEnabled(false);
    } else {
        loadSelectedMeetingViews();   // gombok visszaállítása a kiválasztás szerint
    }
}

void MainWindow::onRecordingFinished(tanara::Meeting meeting) {
    statusBar()->showMessage(
        QStringLiteral("Felvétel kész: %1").arg(meeting.title), 5000);
    // A modell a store jelzéseire magától frissül; ettől függetlenül friss.
}

void MainWindow::onTableContextMenu(const QPoint& pos) {
    // Csak akkor van értelme, ha a kattintás egy érvényes soron áll, ÉS van
    // kiválasztott meeting (a kijelölés a jobbgombra is áthelyeződik a SelectRows
    // viselkedéssel; ha mégsem, a renameSelectedMeeting maga is no-op).
    const QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid())
        return;

    QMenu menu(this);
    QAction* renameAct = menu.addAction(QStringLiteral("Átnevezés…"));
    connect(renameAct, &QAction::triggered, this, &MainWindow::renameSelectedMeeting);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::renameSelectedMeeting() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller || m.id.isEmpty())
        return;

    bool accepted = false;
    const QString newTitle = QInputDialog::getText(
        this,
        QStringLiteral("Megbeszélés átnevezése"),
        QStringLiteral("Új cím:"),
        QLineEdit::Normal,
        m.title,
        &accepted);

    if (!accepted)
        return;
    const QString trimmed = newTitle.trimmed();
    // Üres vagy változatlan cím → nincs teendő (a tábla a meetingUpdated jelre
    // magától frissül, így itt nem kell kézzel újratölteni).
    if (trimmed.isEmpty() || trimmed == m.title)
        return;

    m_controller->renameMeeting(m.id, trimmed);
}

void MainWindow::openSettings() {
    SettingsDialog dlg(m_controller, this);
    dlg.exec();
}

void MainWindow::openPeopleManager() {
    // Nem-modális, hogy a háttérben az átnevezés/törlés hatása (speakerMapChanged
    // → reloadTranscriptView) azonnal látszódjon a nyitott Átirat-nézeten.
    auto* dlg = new PeopleManagerDialog(m_controller, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

} // namespace tanara_gui
