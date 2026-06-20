#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "RecordBar.h"
#include "SettingsDialog.h"
#include "MeetingTableModel.h"
#include "MeetingItemDelegate.h"
#include "TranscriptPlayer.h"
#include "PeopleManagerDialog.h"
#include "FloatingRecorder.h"
#include "TracksPanel.h"
#include "ParticipantsDialog.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

#include <QTableView>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QTextBrowser>
#include <QTabWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QToolButton>
#include <QSplitter>
#include <QFrame>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QDesktopServices>
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
#include <QLocale>
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

    // Egyoszlopos lista a mockup szerint: az Idő és Hossz oszlopot ELREJTJÜK (az
    // értékeiket a delegate a Név-oszlop 3. sorában rajzolja ki), a Név-oszlop nyúlik.
    // A rendezés EditRole szerint megy, így rejtett oszlopra is rendezhető (ColTime).
    m_table->setColumnHidden(MeetingTableModel::ColTime, true);
    m_table->setColumnHidden(MeetingTableModel::ColDuration, true);

    // A Név-oszlop 3 soros renderelése (név félkövér / státusz-badge-ek / emberi
    // dátum + hossz). A delegate CSAK rajzol; a rendezés a proxy EditRole-ján megy.
    m_table->setItemDelegateForColumn(MeetingTableModel::ColName,
                                      new MeetingItemDelegate(this));

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
        // A lekeverés (Újrakeverés) befejeztével a busy-jelzőt KI kell kapcsolni — a
        // regenerateMixdown a végén jobProgress-t emittál (ami bekapcsolja a busy-t),
        // de korábban semmi nem kapcsolta ki. Ez a kötés zárja le.
        connect(m_controller, &tanara::AppController::mixdownUpdated, this,
                [this](const QString&, bool ok) {
                    setBusy(false);
                    statusBar()->showMessage(
                        ok ? QStringLiteral("Lekeverés kész.")
                           : QStringLiteral("A lekeverés sikertelen."), 4000);
                });
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
        m_recordBar = nullptr;              // a FloatingRecorder volt a szülője → már törölve
    } else if (m_recordBar) {
        // Soha nem volt leválasztva → a parentless RecordBar-t mi takarítjuk el.
        delete m_recordBar;
        m_recordBar = nullptr;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    // A STATIKUS vázat a MainWindow.ui adja (Designerből szerkeszthető); a
    // tag-pointereket innen kötjük be, a viselkedés/dinamika alább, kódban marad.
    ui = new Ui::MainWindow;
    ui->setupUi(this);

    m_table            = ui->table;
    m_tabs             = ui->tabs;
    m_reviewStack      = ui->reviewStack;
    m_transcriptPlayer = ui->transcriptPlayer;
    m_summaryView      = ui->summaryView;
    m_tracksPanel      = ui->tracksPanel;

    m_newRecordingBtn  = ui->newRecordingBtn;
    m_peopleBtn        = ui->peopleBtn;
    m_settingsBtn      = ui->settingsBtn;

    m_titleLabel       = ui->titleLabel;
    m_metaLabel        = ui->metaLabel;
    m_tracksBtn        = ui->tracksBtn;
    m_speakersBar      = ui->speakersBar;
    m_speakersToggle   = ui->speakersToggle;
    m_speakersSummary  = ui->speakersSummary;
    m_speakersEditBtn  = ui->speakersEditBtn;

    m_step2label       = ui->step2label;
    m_transcribeBtn    = ui->transcribeBtn;

    // State A pipeline-panel: átirat ELŐTTI, hang-alapú résztvevő-azonosítás belépője.
    // (A Beszélők-sáv „✏ Hozzárendelés…" menüje csak átirat MELLETT látszik; itt a
    // friss, átirat nélküli felvételen is elérhető legyen — az onParticipantsClicked
    // CSAK az audio-sávokból dolgozik, nem kell hozzá átirat.)
    m_identifyParticipantsBtn =
        new QPushButton(QStringLiteral("👥  Résztvevők azonosítása (hang alapján)"), this);
    m_identifyParticipantsBtn->setToolTip(QStringLiteral(
        "A résztvevők megtippelése a hangsávok alapján — átirat nélkül is futtatható."));
    if (auto* stepLayout = qobject_cast<QVBoxLayout*>(ui->stepBox->layout())) {
        // A „③ Összefoglaló" sor (step3) elé, az átirat-doboz után.
        const int idx = stepLayout->indexOf(ui->step3);
        if (idx >= 0)
            stepLayout->insertWidget(idx, m_identifyParticipantsBtn);
        else
            stepLayout->addWidget(m_identifyParticipantsBtn);
    }
    connect(m_identifyParticipantsBtn, &QPushButton::clicked,
            this, &MainWindow::onParticipantsClicked);

    // --- meeting-tábla viselkedése (kód) ---
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    // A sormagasság a delegate sizeHint-jét kövesse (3 soros sor), különben a
    // QTableView fix egysoros magasságot ad és a tartalom egymásba lóg.
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setVisible(false);   // egyoszlopos lista — nincs Idő/Hossz/Név fejléc
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTableContextMenu);

    // --- felvétel-vezérlő: ALAPBÓL leválasztott (a fő ablakban nincs beágyazott
    //     recorder). A RecordBar parentless, hogy a FloatingRecorder reparentálhassa;
    //     a tényleges pop-out a popOutRecorder()-ben jön létre (igény szerint). ---
    m_recordBar = new RecordBar(m_controller, nullptr);
    m_recordBar->setVisible(false);

    // --- fülek (a custom widgetek a .ui-ban promotálva; itt csak bekötés) ---
    m_transcriptPlayer->setController(m_controller);
    // „Mindet kezel… (Emberek)" a beszélő-legendából → globális Személyek-kezelő.
    connect(m_transcriptPlayer, &TranscriptPlayer::managePeopleRequested,
            this, &MainWindow::openPeopleManager);
    m_summaryView->setOpenExternalLinks(true);
    m_tracksPanel->setController(m_controller);

    // Az Összefoglaló-fül kétállapotú: ha van összefoglaló → summaryView; ha nincs →
    // középen „✨ Összefoglaló generálása" gomb (canRun(Summarize) szerint kapuzva).
    // A summaryView-t kivesszük a tab-ból, és egy QStackedWidget-be tesszük helyette.
    {
        const int summaryIdx = m_tabs->indexOf(m_summaryView);
        const QString summaryTitle = (summaryIdx >= 0)
            ? m_tabs->tabText(summaryIdx) : QStringLiteral("Összefoglaló");
        if (summaryIdx >= 0)
            m_tabs->removeTab(summaryIdx);

        m_summaryStack = new QStackedWidget(m_tabs);
        m_summaryStack->addWidget(m_summaryView);     // page 0: kész összefoglaló

        m_summaryEmptyPage = new QWidget(m_summaryStack);
        auto* el = new QVBoxLayout(m_summaryEmptyPage);
        el->addStretch(1);
        auto* emptyLbl = new QLabel(
            QStringLiteral("Még nincs összefoglaló ehhez a megbeszéléshez."),
            m_summaryEmptyPage);
        emptyLbl->setAlignment(Qt::AlignCenter);
        emptyLbl->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
        m_generateSummaryBtn = new QPushButton(
            QStringLiteral("✨  Összefoglaló generálása"), m_summaryEmptyPage);
        m_generateSummaryBtn->setStyleSheet(
            QStringLiteral("QPushButton { font-weight: bold; padding: 8px 18px; }"));
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(1);
        btnRow->addWidget(m_generateSummaryBtn);
        btnRow->addStretch(1);
        el->addWidget(emptyLbl);
        el->addSpacing(10);
        el->addLayout(btnRow);
        el->addStretch(1);
        m_summaryStack->addWidget(m_summaryEmptyPage);  // page 1: üres + generálás-gomb

        // A Sávok-fül elé szúrjuk vissza (Átirat | Összefoglaló | Sávok sorrend).
        const int tracksIdx = m_tabs->indexOf(m_tracksPanel);
        if (tracksIdx >= 0)
            m_tabs->insertTab(tracksIdx, m_summaryStack, summaryTitle);
        else
            m_tabs->addTab(m_summaryStack, summaryTitle);
    }

    // --- a TranscriptPlayer lejátszó-sávját KIEMELJÜK a jobb pane aljára (mindig
    //     látható, a fülektől függetlenül; a logika a TranscriptPlayerben marad). ---
    if (QWidget* pb = m_transcriptPlayer->playerBar()) {
        ui->playerBarHostLayout->addWidget(pb);   // reparent a host-frame-be
    }

    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);

    statusBar()->showMessage(QStringLiteral("Készen áll"));
    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);            // indeterminált (pörgő) busy-jelző
    m_busyBar->setMaximumWidth(160);
    m_busyBar->setTextVisible(false);
    m_busyBar->setVisible(false);
    statusBar()->addPermanentWidget(m_busyBar);

    // --- felső sáv akciói (a meglévő logikát hívják) ---
    connect(m_newRecordingBtn, &QPushButton::clicked, this, &MainWindow::popOutRecorder);
    connect(m_peopleBtn, &QPushButton::clicked, this, &MainWindow::openPeopleManager);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    // --- jobb pane: beszélők-sáv + Sávok + kapu-panel + összefoglaló-generálás ---
    connect(m_tracksBtn, &QToolButton::clicked, this, &MainWindow::onTracksToggleClicked);
    connect(m_speakersToggle, &QToolButton::clicked, this, &MainWindow::onSpeakersToggleClicked);
    connect(m_speakersEditBtn, &QPushButton::clicked, this, &MainWindow::onSpeakersEditClicked);
    connect(m_transcribeBtn, &QPushButton::clicked, this, &MainWindow::onTranscribeClicked);
    connect(m_generateSummaryBtn, &QPushButton::clicked, this, &MainWindow::onSummarizeClicked);

    // Üres induló állapot (nincs kiválasztott meeting).
    m_titleLabel->clear();
    m_metaLabel->clear();
    m_speakersBar->setVisible(false);
    m_tracksBtn->setEnabled(false);
    m_transcribeBtn->setEnabled(false);
    m_generateSummaryBtn->setEnabled(false);
    if (m_identifyParticipantsBtn) m_identifyParticipantsBtn->setEnabled(false);
    m_reviewStack->setCurrentWidget(ui->tabsPage);
}

void MainWindow::buildMenu() {
    // A gyakori akciók a felső sávon élnek; a menü a teljességhez marad meg.
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&Fájl"));
    QAction* newRecAct = fileMenu->addAction(QStringLiteral("🔴  Új felvétel…"));
    connect(newRecAct, &QAction::triggered, this, &MainWindow::popOutRecorder);
    fileMenu->addSeparator();
    QAction* settingsAct = fileMenu->addAction(QStringLiteral("Beállítások…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    QAction* peopleAct = fileMenu->addAction(QStringLiteral("Személyek…"));
    connect(peopleAct, &QAction::triggered, this, &MainWindow::openPeopleManager);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(QStringLiteral("Kilépés"));
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&Nézet"));
    QAction* tracksAct = viewMenu->addAction(QStringLiteral("Sávok"));
    tracksAct->setToolTip(QStringLiteral("A kiválasztott megbeszélés hangsávjai."));
    connect(tracksAct, &QAction::triggered, this, &MainWindow::onTracksToggleClicked);
    QAction* participantsAct =
        viewMenu->addAction(QStringLiteral("Résztvevők azonosítása (hang alapján)…"));
    participantsAct->setToolTip(QStringLiteral(
        "A kiválasztott felvétel résztvevőinek megtippelése a hangsávok alapján "
        "(átirat nélkül is)."));
    connect(participantsAct, &QAction::triggered, this, &MainWindow::onParticipantsClicked);
    QAction* recWinAct = viewMenu->addAction(QStringLiteral("Felvétel-ablak előtérbe"));
    recWinAct->setToolTip(QStringLiteral(
        "A leválasztott felvétel-vezérlő ablakot előtérbe hozza (vagy megnyitja)."));
    connect(recWinAct, &QAction::triggered, this, &MainWindow::popOutRecorder);
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
    if (md.isEmpty()) {
        // Üres állapot: a kétállapotú stack a „✨ generálás" gomb oldalára vált.
        // A gomb kapuzását az updateReviewGating(canRun(Summarize)) intézi.
        m_summaryStack->setCurrentWidget(m_summaryEmptyPage);
    } else {
        m_summaryView->setMarkdown(md);
        m_summaryStack->setCurrentWidget(m_summaryView);
    }
}

QString MainWindow::humanDate(const tanara::Meeting& m) {
    if (!m.startedAt.isValid())
        return {};
    // „2026. június 18. · 1ó 32p" stílus (a mockup metaLabel-je).
    return QLocale(QLocale::Hungarian)
        .toString(m.startedAt, QStringLiteral("yyyy. MMMM d."));
}

QString MainWindow::durationHuman(qint64 ms) {
    const qint64 totalSec = ms / 1000;
    const qint64 hh = totalSec / 3600;
    const qint64 mm = (totalSec % 3600) / 60;
    if (hh > 0)
        return QStringLiteral("%1ó %2p").arg(hh).arg(mm);
    return QStringLiteral("%1p").arg(mm);
}

void MainWindow::reloadHeader(const tanara::Meeting& m) {
    m_titleLabel->setText(m.title);
    const QString date = humanDate(m);
    const QString dur = durationHuman(m.durationMs);
    if (!date.isEmpty() && m.durationMs > 0)
        m_metaLabel->setText(date + QStringLiteral(" · ") + dur);
    else if (!date.isEmpty())
        m_metaLabel->setText(date);
    else
        m_metaLabel->setText(dur);
}

void MainWindow::reloadSpeakersBar(const tanara::Meeting& m) {
    // Összecsukott összefoglaló: a beszélők darabszáma + a hozzárendelt nevek listája.
    // A nyers beszélő-címkék a speakerMap kulcsai; a valódi nevek az értékei.
    const int n = m.speakerMap.size();
    m_speakersToggle->setText(
        (m_speakersExpanded ? QStringLiteral("▾ Beszélők (%1)")
                            : QStringLiteral("▸ Beszélők (%1)")).arg(n));

    QStringList named;
    int unknown = 0;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it) {
        if (it.value().trimmed().isEmpty())
            ++unknown;
        else
            named << it.value().trimmed();
    }
    named.removeDuplicates();
    QStringList parts = named;
    if (unknown > 0)
        parts << QStringLiteral("%1 ismeretlen").arg(unknown);
    m_speakersSummary->setText(parts.join(QStringLiteral(" · ")));

    // A beszélők-sáv csak akkor érdemi, ha van átirat (abból jönnek a beszélők).
    m_speakersBar->setVisible(m.hasTranscript);
    m_speakersEditBtn->setEnabled(true);
}

void MainWindow::updateReviewGating(const tanara::Meeting& m) {
    // State A (nincs átirat) → vezérelt pipeline-panel a fülek helyett.
    // Van átirat → normál fülek; az Összefoglaló-fül üres állapota maga kapuz.
    if (!m.hasTranscript) {
        m_reviewStack->setCurrentWidget(ui->stepPage);

        // „Résztvevők (hang alapján)" — engedélyezve, amint van legalább egy aktív
        // hangsáv (átirat NEM kell; az onParticipantsClicked csak az audióból dolgozik).
        if (m_identifyParticipantsBtn) {
            bool hasActiveTrack = false;
            for (const tanara::Track& t : m.tracks) {
                if (t.active) { hasActiveTrack = true; break; }
            }
            m_identifyParticipantsBtn->setEnabled(hasActiveTrack);
            m_identifyParticipantsBtn->setToolTip(
                hasActiveTrack
                    ? QStringLiteral("A résztvevők megtippelése a hangsávok alapján — "
                                     "átirat nélkül is futtatható.")
                    : QStringLiteral("Nincs aktív hangsáv ehhez a felvételhez."));
        }

        // ② Átirat — a gomb engedélyezettsége/CTA-ja a canRun(Transcribe) szerint.
        const tanara::ReadinessResult r =
            m_controller ? m_controller->canRun(tanara::WorkflowStep::Transcribe, m.id)
                         : tanara::ReadinessResult{};
        m_transcribeBtn->setEnabled(r.runnable);
        if (r.runnable) {
            m_step2label->setText(QStringLiteral("<b>② Átirat</b>"));
            m_transcribeBtn->setText(QStringLiteral("Átírás indítása ▸"));
            m_transcribeBtn->setToolTip(QString());
        } else {
            // Blokkolt (jellemzően provider-konfig) → „Előbb: <detail>" + Beállítás CTA.
            m_step2label->setText(
                QStringLiteral("<b>② Átirat</b><br><span style='color:#b35900;'>Előbb: %1</span>")
                    .arg(r.detail.toHtmlEscaped()));
            if (r.blockerKind == tanara::BlockerKind::ProviderConfig
                || r.blockerKind == tanara::BlockerKind::Auth) {
                m_transcribeBtn->setText(QStringLiteral("⚙ Beállítás…"));
                m_transcribeBtn->setEnabled(true);   // a Beállítás-nyitás mindig megy
            } else {
                m_transcribeBtn->setText(QStringLiteral("Átírás indítása ▸"));
                m_transcribeBtn->setEnabled(false);
            }
            m_transcribeBtn->setToolTip(r.detail);
        }
        return;
    }

    // Van átirat → fülek. Az Összefoglaló generálás-gombja canRun(Summarize) szerint.
    m_reviewStack->setCurrentWidget(ui->tabsPage);
    const tanara::ReadinessResult rs =
        m_controller ? m_controller->canRun(tanara::WorkflowStep::Summarize, m.id)
                     : tanara::ReadinessResult{};
    if (rs.runnable) {
        m_generateSummaryBtn->setText(QStringLiteral("✨  Összefoglaló generálása"));
        m_generateSummaryBtn->setEnabled(true);
        m_generateSummaryBtn->setToolTip(QString());
    } else if (rs.blockerKind == tanara::BlockerKind::ProviderConfig
               || rs.blockerKind == tanara::BlockerKind::Auth) {
        // Provider-konfig/auth blokk → „⚙ Beállítás…" CTA (mint az Átírásnál), a
        // gomb engedélyezve, és a Beállításokat nyitja (onSummarizeClicked kapuz).
        m_generateSummaryBtn->setText(QStringLiteral("⚙  Beállítás…"));
        m_generateSummaryBtn->setEnabled(true);
        m_generateSummaryBtn->setToolTip(rs.detail);
    } else {
        // MeetingState (pl. nincs átirat) → marad a tiltott + tooltip viselkedés.
        m_generateSummaryBtn->setText(QStringLiteral("✨  Összefoglaló generálása"));
        m_generateSummaryBtn->setEnabled(false);
        m_generateSummaryBtn->setToolTip(QStringLiteral("Előbb: %1").arg(rs.detail));
    }
}

void MainWindow::loadSelectedMeetingViews() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);

    if (!ok) {
        m_currentMeetingId.clear();
        m_transcriptPlayer->clearMeeting();
        m_summaryView->clear();
        m_tracksPanel->clearMeeting();
        m_titleLabel->clear();
        m_metaLabel->clear();
        m_speakersBar->setVisible(false);
        m_tracksBtn->setEnabled(false);
        m_transcribeBtn->setEnabled(false);
        m_generateSummaryBtn->setEnabled(false);
        if (m_identifyParticipantsBtn) m_identifyParticipantsBtn->setEnabled(false);
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        return;
    }

    m_currentMeetingId = m.id;
    m_tracksBtn->setEnabled(true);
    reloadHeader(m);
    reloadTranscriptView(m);   // betölti a segments.json-t + hangforrást a lejátszóba
    reloadSummaryView(m);
    m_tracksPanel->setMeeting(m);
    reloadSpeakersBar(m);
    updateReviewGating(m);
}

void MainWindow::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
    loadSelectedMeetingViews();
}

void MainWindow::onTranscribeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    // Kapuzás: ha az átírás provider-konfig/auth miatt blokkolt, a gomb „⚙ Beállítás…"
    // CTA-ként viselkedik → a Beállítások-ablakot nyitja, nem indít átírást.
    const tanara::ReadinessResult r =
        m_controller->canRun(tanara::WorkflowStep::Transcribe, m.id);
    if (!r.runnable) {
        if (r.blockerKind == tanara::BlockerKind::ProviderConfig
            || r.blockerKind == tanara::BlockerKind::Auth) {
            openSettings();
        } else {
            statusBar()->showMessage(
                QStringLiteral("Nem indítható: ") + r.detail, 6000);
        }
        return;
    }
    setBusy(true, QStringLiteral("Átírás indítása…"));
    m_controller->transcribeMeeting(m.id);
}

void MainWindow::onSummarizeClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    // Kapuzás (az Átírás CTA-mintáját tükrözve): ha provider-konfig/auth miatt blokkolt,
    // a gomb „⚙ Beállítás…" CTA-ként a Beállításokat nyitja, nem indít összefoglalót.
    const tanara::ReadinessResult rs =
        m_controller->canRun(tanara::WorkflowStep::Summarize, m.id);
    if (!rs.runnable) {
        if (rs.blockerKind == tanara::BlockerKind::ProviderConfig
            || rs.blockerKind == tanara::BlockerKind::Auth) {
            openSettings();
        } else {
            statusBar()->showMessage(
                QStringLiteral("Nem indítható: ") + rs.detail, 6000);
        }
        return;
    }
    setBusy(true, QStringLiteral("Összefoglaló készítése…"));
    m_controller->summarizeMeeting(m.id);
}

void MainWindow::onParticipantsClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    ParticipantsDialog dlg(m_controller, m.id, this);
    dlg.exec();
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
        reloadSpeakersBar(m);
        updateReviewGating(m);          // most már van átirat → fülek + Összefoglaló-kapu
        m_reviewStack->setCurrentWidget(ui->tabsPage);
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
        reloadSummaryView(m);                       // → a stack a kész összefoglalóra vált
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        m_tabs->setCurrentWidget(m_summaryStack);   // az Összefoglaló-fül (stack)
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
    if (ok) {
        reloadTranscriptView(m);
        reloadSpeakersBar(m);
    }
}

void MainWindow::setBusy(bool busy, const QString& msg) {
    if (m_busyBar) m_busyBar->setVisible(busy);
    if (!msg.isEmpty()) statusBar()->showMessage(msg);
    if (busy) {
        // Futás közben a kapuzott akció-gombokat tiltjuk (a kapuzást újraértékeli a
        // setBusy(false) → loadSelectedMeetingViews → updateReviewGating).
        if (m_transcribeBtn)      m_transcribeBtn->setEnabled(false);
        if (m_generateSummaryBtn) m_generateSummaryBtn->setEnabled(false);
        if (m_speakersEditBtn)    m_speakersEditBtn->setEnabled(false);
        if (m_identifyParticipantsBtn) m_identifyParticipantsBtn->setEnabled(false);
    } else {
        loadSelectedMeetingViews();   // gombok visszaállítása a kiválasztás/kapuzás szerint
    }
}

void MainWindow::onTracksToggleClicked() {
    // A Sávok-funkció elérhető marad: a fülek közti Sávok-fülre vált (van átirat-nézet),
    // ill. State A-ban a fülekre kapcsol, hogy a Sávok látszódjon.
    if (!m_tracksPanel)
        return;
    m_reviewStack->setCurrentWidget(ui->tabsPage);
    m_tabs->setCurrentWidget(m_tracksPanel);
}

void MainWindow::onSpeakersToggleClicked() {
    // A beszélők-sáv ki-/becsukása: kinyitva az Átirat-fülre váltunk, ahol a
    // TranscriptPlayer per-beszélő sorai (átnevezés/teszt/meghallgatás) élnek.
    m_speakersExpanded = !m_speakersExpanded;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok)
        m_speakersToggle->setText(
            (m_speakersExpanded ? QStringLiteral("▾ Beszélők (%1)")
                                : QStringLiteral("▸ Beszélők (%1)")).arg(m.speakerMap.size()));
    if (m_speakersExpanded) {
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        m_tabs->setCurrentWidget(m_transcriptPlayer);
    }
}

void MainWindow::onSpeakersEditClicked() {
    // „✏ Hozzárendelés…": a meglévő Résztvevők (ParticipantsDialog) és Azonosítás
    // (autoIdentifyMeeting) akciókat ajánlja fel egy kis menüben — a per-beszélő
    // átnevezés/teszt továbbra is a TranscriptPlayer soraiban érhető el.
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok)
        return;
    QMenu menu(this);
    QAction* participantsAct = menu.addAction(QStringLiteral("👥  Résztvevők…"));
    QAction* identifyAct = menu.addAction(QStringLiteral("🔍  Beszélők azonosítása (hang alapján)"));
    QAction* chosen = menu.exec(m_speakersEditBtn->mapToGlobal(
        QPoint(0, m_speakersEditBtn->height())));
    if (chosen == participantsAct)
        onParticipantsClicked();
    else if (chosen == identifyAct)
        onIdentifyClicked();
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

    QAction* openFolderAct = menu.addAction(QStringLiteral("📂  Mappa megnyitása"));
    connect(openFolderAct, &QAction::triggered, this, [this]() {
        bool ok = false;
        const tanara::Meeting m = selectedMeeting(&ok);
        if (!ok || m.folder.isEmpty())
            return;
        // A meeting mappáját az OS fájlböngészőjében nyitja.
        QDesktopServices::openUrl(QUrl::fromLocalFile(m.folder));
    });

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
    // A felvevő ALAPBÓL leválasztott (a fő ablak jobb pane-je tiszta review). Az
    // „Új felvétel" gomb a leválasztott FloatingRecordert nyitja meg / hozza előtérbe.
    if (m_floatingRecorder) {                 // már kint van → csak előtérbe
        m_floatingRecorder->show();
        m_floatingRecorder->raise();
        m_floatingRecorder->activateWindow();
        return;
    }
    // Az új felvevő-elrendezés MÁR eleve kompakt (kétoszlopos, VU alapból rejtve), ezért
    // a régi Kompakt-redukció (cím elrejt, eszközöket szűr, szinteket erőből felnyit)
    // NEM kell — az töri szét a lebegő ablakot. Teljes nézettel jelenítjük meg.
    m_recordBar->setViewMode(RecordBar::ViewMode::Full);
    // A RecordBar-t a FloatingRecorder ctora reparentálja magába. parent=nullptr →
    // ÖNÁLLÓ top-level ablak (saját tálca-bejegyzés, NEM minimalizálódik a főablakkal).
    m_floatingRecorder = new FloatingRecorder(m_controller, m_recordBar, nullptr);
    connect(m_floatingRecorder, &FloatingRecorder::dockRequested,
            this, &MainWindow::dockRecorder);
    m_recordBar->show();
    m_floatingRecorder->show();
    m_floatingRecorder->raise();
    m_floatingRecorder->activateWindow();
}

void MainWindow::dockRecorder() {
    // „Dokkolás" az új modellben = a leválasztott felvétel-ablak elrejtése (a fő
    // ablakban nincs hova beágyazni). A RecordBar a FloatingRecorderben marad, így
    // egy esetleges futó felvétel/állapot megmarad; az „Új felvétel" újra előhozza.
    if (!m_floatingRecorder)
        return;
    m_floatingRecorder->hide();
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
