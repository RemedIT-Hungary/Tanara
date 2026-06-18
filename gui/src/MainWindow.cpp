#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "RecordBar.h"
#include "SettingsDialog.h"
#include "MeetingTableModel.h"
#include "TranscriptPlayer.h"
#include "PeopleManagerDialog.h"
#include "FloatingRecorder.h"
#include "TracksPanel.h"

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
#include <QAction>
#include <QStyle>
#include <QLabel>
#include <QSignalBlocker>
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
        connect(m_controller->store(), &tanara::MeetingStore::meetingRemoved,
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
        connect(m_controller, &tanara::AppController::tracksChanged,
                this, [this](const QString& id) {
                    reloadMeetings();
                    if (id == m_currentMeetingId) loadSelectedMeetingViews();
                });

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
    delete ui;
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
    // A különálló (parent nélküli) lebegő rögzítő-ablakot kézzel zárjuk, különben
    // a főablak bezárása után is kint maradna (önálló top-level).
    if (m_floatingRecorder) {
        m_floatingRecorder->disconnect(this);
        m_floatingRecorder->close();
        delete m_floatingRecorder;          // a benne lévő RecordBar-t is elviszi (kilépéskor OK)
        m_floatingRecorder = nullptr;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    // A STATIKUS vázat a MainWindow.ui adja (Designerből szerkeszthető); a
    // tag-pointereket innen kötjük be, a viselkedés/dinamika alább, kódban marad.
    ui = new Ui::MainWindow;
    ui->setupUi(this);

    m_table            = ui->table;
    m_rightLayout      = ui->rightLayout;
    m_tabs             = ui->tabs;
    m_transcriptPlayer = ui->transcriptPlayer;
    m_summaryView      = ui->summaryView;
    m_tracksPanel      = ui->tracksPanel;
    m_transcribeBtn    = ui->transcribeBtn;
    m_summarizeBtn     = ui->summarizeBtn;
    m_identifyBtn      = ui->identifyBtn;

    // --- meeting-tábla viselkedése (kód) ---
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTableContextMenu);

    // --- felvétel-vezérlő + helykitöltő a jobb pane tetejére (a controllert igénylik) ---
    m_recordBar = new RecordBar(m_controller, ui->rightPane);
    m_rightLayout->insertWidget(0, m_recordBar);

    m_dockPlaceholder = new QWidget(ui->rightPane);
    {
        auto* pl = new QVBoxLayout(m_dockPlaceholder);
        pl->setContentsMargins(0, 0, 0, 0);
        auto* lbl = new QLabel(
            QStringLiteral("🎙 A felvétel-vezérlő külön ablakban fut."), m_dockPlaceholder);
        lbl->setWordWrap(true);
        auto* backBtn = new QPushButton(QStringLiteral("Vissza a főablakba"), m_dockPlaceholder);
        pl->addWidget(lbl);
        pl->addWidget(backBtn, 0, Qt::AlignLeft);
        connect(backBtn, &QPushButton::clicked, this, [this]() {
            if (m_popOutAct) m_popOutAct->setChecked(false);   // → dockRecorder()
        });
    }
    m_dockPlaceholder->setVisible(false);
    m_rightLayout->insertWidget(1, m_dockPlaceholder);

    // --- fülek (a custom widgetek a .ui-ban promotálva; itt csak bekötés) ---
    m_transcriptPlayer->setController(m_controller);
    m_summaryView->setOpenExternalLinks(true);
    m_tracksPanel->setController(m_controller);

    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);

    statusBar()->showMessage(QStringLiteral("Készen áll"));
    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);            // indeterminált (pörgő) busy-jelző
    m_busyBar->setMaximumWidth(160);
    m_busyBar->setTextVisible(false);
    m_busyBar->setVisible(false);
    statusBar()->addPermanentWidget(m_busyBar);

    connect(m_transcribeBtn, &QPushButton::clicked, this, &MainWindow::onTranscribeClicked);
    connect(m_summarizeBtn, &QPushButton::clicked, this, &MainWindow::onSummarizeClicked);
    connect(m_identifyBtn, &QPushButton::clicked, this, &MainWindow::onIdentifyClicked);

    m_transcribeBtn->setEnabled(false);
    m_summarizeBtn->setEnabled(false);
    m_identifyBtn->setEnabled(false);
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

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&Nézet"));
    m_popOutAct = viewMenu->addAction(QStringLiteral("Felvétel külön ablakban"));
    m_popOutAct->setCheckable(true);
    m_popOutAct->setToolTip(QStringLiteral(
        "A felvétel-vezérlőt önálló, mindig-felül kapcsolható, tálcázható ablakba helyezi."));
    connect(m_popOutAct, &QAction::toggled, this, [this](bool on) {
        if (on) popOutRecorder();
        else    dockRecorder();
    });
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
    const QString mixdown = QDir(m.folder).filePath(
        m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
    if (QFileInfo::exists(mixdown))
        return mixdown;
    // Nincs mixdown (régi felvétel vagy sikertelen keverés) → fallback: a legnagyobb
    // AKTÍV sáv, hogy a lejátszás akkor is működjön. A szegmens-időbélyegek globálisak
    // és minden sáv t=0-ról indul, így a kiemelés/odaugrás egyetlen sávra is időhelyes.
    QString best;
    qint64 bestSize = -1;
    for (const tanara::Track& t : m.tracks) {
        if (!t.active)
            continue;
        const QString p = QDir(m.folder).filePath(t.file);
        const QFileInfo fi(p);
        if (fi.exists() && fi.size() > bestSize) {
            bestSize = fi.size();
            best = p;
        }
    }
    return best.isEmpty() ? mixdown : best;
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
    m_identifyBtn->setEnabled(enable);

    if (!ok) {
        m_currentMeetingId.clear();
        m_transcriptPlayer->clearMeeting();
        m_summaryView->clear();
        m_tracksPanel->clearMeeting();
        return;
    }

    m_currentMeetingId = m.id;
    reloadTranscriptView(m);   // betölti a segments.json-t + hangforrást a lejátszóba
    reloadSummaryView(m);
    m_tracksPanel->setMeeting(m);
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

void MainWindow::onIdentifyClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    setBusy(true, QStringLiteral("Beszélők azonosítása a hang-lenyomatok alapján…"));
    m_controller->autoIdentifyMeeting(m.id);   // szinkron (ffmpeg + onnx)
    setBusy(false);
    statusBar()->showMessage(QStringLiteral("Azonosítás kész."), 4000);
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
        if (m_identifyBtn)   m_identifyBtn->setEnabled(false);
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
    menu.addSeparator();
    QAction* deleteAct = menu.addAction(QStringLiteral("🗑  Törlés…"));
    // Vörös, „veszélyes” kiemelés a törlés-akcióhoz.
    deleteAct->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    menu.setStyleSheet(QStringLiteral(
        "QMenu::item:selected { } "));   // alapértelmezett kiemelés megtartása
    {
        QFont df = deleteAct->font();
        df.setBold(true);
        deleteAct->setFont(df);
    }
    connect(deleteAct, &QAction::triggered, this, &MainWindow::deleteSelectedMeeting);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::deleteSelectedMeeting() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller || m.id.isEmpty())
        return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Megbeszélés törlése"));
    box.setText(QStringLiteral("Biztosan törlöd: „%1”?").arg(m.title));
    box.setInformativeText(QStringLiteral(
        "A felvétel (hangsávok), az átirat és az összefoglaló is VÉGLEGESEN törlődik. "
        "Ez nem visszavonható."));
    QPushButton* del = box.addButton(QStringLiteral("Törlés"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Mégse"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(box.buttons().last()));
    box.exec();
    if (box.clickedButton() != del)
        return;

    m_controller->deleteMeeting(m.id);   // store meetingRemoved → reloadMeetings
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

void MainWindow::popOutRecorder() {
    if (m_floatingRecorder) {                 // már kint van → csak előtérbe
        m_floatingRecorder->raise();
        m_floatingRecorder->activateWindow();
        return;
    }
    // Lebegőben a kompakt nézet a default (a sarokba illő, kicsi vezérlő).
    m_recordBar->setViewMode(RecordBar::ViewMode::Compact);
    // A RecordBar-t a FloatingRecorder ctora reparentálja magába; itt a
    // helykitöltőt mutatjuk a jobb pane-ben. parent=nullptr → ÖNÁLLÓ top-level
    // ablak (saját tálca-bejegyzés, NEM minimalizálódik a főablakkal).
    m_floatingRecorder = new FloatingRecorder(m_controller, m_recordBar, nullptr);
    connect(m_floatingRecorder, &FloatingRecorder::dockRequested,
            this, &MainWindow::dockRecorder);
    m_dockPlaceholder->setVisible(true);
    m_floatingRecorder->show();
    m_floatingRecorder->raise();
    m_floatingRecorder->activateWindow();
}

void MainWindow::dockRecorder() {
    if (!m_floatingRecorder)
        return;
    // A RecordBar-t visszaillesztjük a jobb pane tetejére (a helykitöltő elé).
    m_rightLayout->insertWidget(0, m_recordBar);
    m_recordBar->setViewMode(RecordBar::ViewMode::Full);   // dokkolva a teljes nézet
    m_recordBar->show();
    m_dockPlaceholder->setVisible(false);

    FloatingRecorder* fr = m_floatingRecorder;
    m_floatingRecorder = nullptr;             // a recordBar már a fő ablak gyermeke
    fr->deleteLater();

    if (m_popOutAct && m_popOutAct->isChecked()) {
        QSignalBlocker block(m_popOutAct);    // ne triggereljen újabb dock/pop-ot
        m_popOutAct->setChecked(false);
    }
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
