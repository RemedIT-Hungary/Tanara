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
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QDesktopServices>
#include <QAction>
#include <QStyle>
#include <QLabel>
#include <QPalette>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QProgressBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QFont>
#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QScrollArea>
#include <QUuid>
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

// Másodlagos/hint felirat: téma-adaptív halvány szín a QPalette::PlaceholderText szerepből
// (NEM stylesheet + NEM palette(mid) — a Mid szín dark témában sötét szürke → olvashatatlan).
static void makeHintLabel(QLabel* l, bool small = false) {
    if (!l) return;
    l->setForegroundRole(QPalette::PlaceholderText);
    if (small) {
        QFont f = l->font();
        f.setPointSizeF(qMax(1.0, f.pointSizeF() - 1.0));
        l->setFont(f);
    }
}

// Húzható méretező-fogantyú egy cél-widget (pl. az elemzés-doboz) alá: lefelé húzva
// magasabb, felfelé alacsonyabb lesz. A célnak fix magasságot ad (min==max), így a
// húzott méret marad. Rajzol egy diszkrét grip-jelet, a kurzor függőleges átméretező.
class HeightGrip : public QWidget {
public:
    explicit HeightGrip(QWidget* target, int minH = 80, QWidget* parent = nullptr)
        : QWidget(parent), m_target(target), m_minH(minH) {
        setCursor(Qt::SizeVerCursor);
        setFixedHeight(9);
        setToolTip(QStringLiteral("Húzd az elemzés-doboz átméretezéséhez"));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(QPen(palette().color(QPalette::PlaceholderText), 1));
        const int cx = width() / 2, cy = height() / 2;
        for (int dx : {-8, 0, 8}) {          // három rövid vonal középen (grip-jelzés)
            p.drawLine(cx + dx - 3, cy - 1, cx + dx + 3, cy - 1);
            p.drawLine(cx + dx - 3, cy + 1, cx + dx + 3, cy + 1);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        m_pressY = e->globalPosition().y();
        m_startH = m_target ? m_target->height() : 0;
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_target || !(e->buttons() & Qt::LeftButton)) return;
        const int h = qMax(m_minH, m_startH + int(e->globalPosition().y() - m_pressY));
        m_target->setMinimumHeight(h);
        m_target->setMaximumHeight(h);
    }
private:
    QWidget* m_target; int m_minH; qreal m_pressY = 0; int m_startH = 0;
};

static QString participantsSummary(QStringList named, int unknownCount, int totalDistinct);

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
        connect(m_controller, &tanara::AppController::topicsReady,
                this, &MainWindow::onTopicsReady);
        connect(m_controller, &tanara::AppController::topicAnalysisQueued,
                this, &MainWindow::onTopicQueued);
        connect(m_controller, &tanara::AppController::topicAnalysisStarted,
                this, &MainWindow::onTopicStarted);
        connect(m_controller, &tanara::AppController::topicAnalysisReady,
                this, &MainWindow::onTopicReady);
        connect(m_controller, &tanara::AppController::topicAnalysisFailed,
                this, &MainWindow::onTopicFailed);
        connect(m_controller, &tanara::AppController::topicAnalysisQueueFinished,
                this, &MainWindow::onTopicQueueFinished);
        connect(m_controller, &tanara::AppController::errorOccurred,
                this, &MainWindow::onError);
        connect(m_controller, &tanara::AppController::jobProgress,
                this, &MainWindow::onJobProgress);
        // Lekeverés haladása (0..100) → determinisztikus, nem-modális állapotsor-progress.
        connect(m_controller, &tanara::AppController::mixdownProgress, this,
                [this](const QString& id, int pct) {
                    m_converting.insert(id);
                    if (m_convertBar) {
                        m_convertBar->setValue(pct);
                        m_convertBar->setVisible(true);
                    }
                    // Ha az épp kiválasztott meeting konvertál, a State A gomb→folyamat váltás.
                    if (id == m_currentMeetingId && m_convertBtn)
                        m_convertBtn->setVisible(false);
                });
        // Lekeverés vége: elrejtjük a progress-t, és ha a kiválasztott meetingé volt, a
        // nézeteket újratöltjük (a lejátszó így a friss kevert fájlt veszi).
        connect(m_controller, &tanara::AppController::mixdownUpdated, this,
                [this](const QString& id, bool ok) {
                    m_converting.remove(id);
                    if (m_convertBar) m_convertBar->setVisible(false);
                    statusBar()->showMessage(
                        ok ? QStringLiteral("Lekeverés kész.")
                           : QStringLiteral("A lekeverés sikertelen."), 4000);
                    if (id == m_currentMeetingId) loadSelectedMeetingViews();
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
    m_speakersBar      = ui->speakersBar;
    m_speakersSummary  = ui->speakersSummary;
    m_speakersEditBtn  = ui->speakersEditBtn;

    m_step2label       = ui->step2label;
    m_transcribeBtn    = ui->transcribeBtn;

    // State A — context-doboz az ② Átirat (transcribe gomb) FÖLÖTT: a felhasználó pár
    // szóban megadja, miről szólt → a Soniox context-envelope-ba megy (pontosabb átirat),
    // és az LLM-összefoglaló is megkapja. Alatta a (tudott) résztvevők + az azonosítás-gomb.
    auto* ctxTitle = new QLabel(QStringLiteral("Miről szólt a meeting?"), this);
    ctxTitle->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));

    m_contextEdit = new QPlainTextEdit(this);
    m_contextEdit->setPlaceholderText(QStringLiteral(
        "Pár szóban a téma, fontos nevek, szakszavak… (opcionális)"));
    m_contextEdit->setMaximumHeight(72);

    auto* ctxHelp = new QLabel(this);
    ctxHelp->setWordWrap(true);
    ctxHelp->setText(QStringLiteral(
        "Ez a kontextus segíti a pontosabb átiratot: az átíró (Soniox) ezzel jobban "
        "dönt a kétes/félreérthető részeknél — nevek, szakszavak, téma."));
    makeHintLabel(ctxHelp, /*small*/ true);

    // (Eddig tudott) résztvevők sora — sáv-címkékből, ill. az azonosítás eredményéből.
    m_participantsResult = new QLabel(this);
    m_participantsResult->setWordWrap(true);
    m_participantsResult->setStyleSheet(QStringLiteral("QLabel { color: palette(text); }"));
    m_participantsResult->setVisible(false);

    m_identifyParticipantsBtn =
        new QPushButton(QStringLiteral("👥  Résztvevők azonosítása (hang alapján)"), this);
    m_identifyParticipantsBtn->setToolTip(QStringLiteral(
        "A résztvevők megtippelése a hangsávok alapján — átirat nélkül is futtatható; "
        "a felismert neveket a context-be is beépíti."));

    // Lekeverés (mixdown) kézi indítója — csak akkor látszik, ha még nincs kevert fájl és
    // nem fut épp a lekeverés. A lekevert, normalizált fájl KÉNYELMES HALLGATÁSRA kell; az
    // átíráshoz nem szükséges (az a per-sáv felvételekből megy).
    m_convertBtn = new QPushButton(QStringLiteral("🎧  Lekeverés készítése (hallgatáshoz)"), this);
    m_convertBtn->setToolTip(QStringLiteral(
        "Egyetlen, hangosságra normalizált hangfájlt készít a sávokból — kényelmes "
        "visszahallgatáshoz. Opcionális: az átíráshoz nem kell."));
    m_convertBtn->setVisible(false);

    if (auto* stepLayout = qobject_cast<QVBoxLayout*>(ui->stepBox->layout())) {
        // Az ② Átirat doboz (step2box) ELÉ, az ① Felvéve után.
        int idx = stepLayout->indexOf(ui->step2box);
        if (idx < 0) idx = stepLayout->count();
        stepLayout->insertWidget(idx,     ctxTitle);
        stepLayout->insertWidget(idx + 1, m_contextEdit);
        stepLayout->insertWidget(idx + 2, ctxHelp);
        stepLayout->insertWidget(idx + 3, m_participantsResult);
        stepLayout->insertWidget(idx + 4, m_identifyParticipantsBtn);
        stepLayout->insertWidget(idx + 5, m_convertBtn);
    }
    connect(m_identifyParticipantsBtn, &QPushButton::clicked,
            this, &MainWindow::onIdentifyParticipants);
    connect(m_convertBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const tanara::Meeting m = selectedMeeting(&ok);
        if (ok && m_controller) m_controller->regenerateMixdown(m.id);
    });

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
    m_summaryView->setOpenExternalLinks(true);
    m_tracksPanel->setController(m_controller);

    // Az Összefoglaló-fül újratervezve: EGY közös, kompakt akció-sáv (mód-választó: Gyors /
    // Témánként, + összecsukható Kontextus + Újragenerálás), alatta egy háromállapotú stack
    // (üres hint / kész összefoglaló / téma-munkaterület). A summaryView-t kivesszük a tabból.
    {
        const int summaryIdx = m_tabs->indexOf(m_summaryView);
        const QString summaryTitle = (summaryIdx >= 0)
            ? m_tabs->tabText(summaryIdx) : QStringLiteral("Összefoglaló");
        if (summaryIdx >= 0)
            m_tabs->removeTab(summaryIdx);

        m_summaryTab  = new QWidget(m_tabs);
        QWidget* sumTab = m_summaryTab;
        auto* sumWrap = new QVBoxLayout(sumTab);
        sumWrap->setContentsMargins(0, 0, 0, 0);
        sumWrap->setSpacing(6);

        // --- akció-sáv: [✨ Gyors] [🧩 Témánként] ……… [⚙ Kontextus] [↻ Újragenerálás] ---
        auto* actionBar = new QHBoxLayout();
        actionBar->setContentsMargins(0, 0, 0, 0);
        m_generateSummaryBtn = new QPushButton(QStringLiteral("✨  Gyors összefoglaló"), sumTab);
        m_generateSummaryBtn->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; }"));
        m_generateSummaryBtn->setToolTip(QStringLiteral(
            "Egy lépésben, egy modell-hívással készít vezetői összefoglalót + teendőket."));
        m_complexBtn = new QPushButton(QStringLiteral("🧩  Témánként"), sumTab);
        m_complexBtn->setToolTip(QStringLiteral(
            "Több körös: a modell kigyűjti a témákat, te szerkeszted, majd témánként részletes "
            "elemzést készít. Pontosabb hosszú/összetett felvételekhez."));
        m_contextToggleBtn = new QPushButton(QStringLiteral("⚙  Kontextus"), sumTab);
        m_contextToggleBtn->setCheckable(true);
        m_contextToggleBtn->setToolTip(QStringLiteral(
            "Pár szó a témáról/nevekről — pontosabb összefoglalót ad. Átirat után is módosítható."));
        m_regenSummaryBtn = new QPushButton(QStringLiteral("↻  Újragenerálás"), sumTab);
        m_regenSummaryBtn->setToolTip(QStringLiteral(
            "A gyors összefoglaló újragenerálása a (módosított) kontextussal — az átiratot nem érinti."));
        actionBar->addWidget(m_generateSummaryBtn);
        actionBar->addWidget(m_complexBtn);
        actionBar->addStretch(1);
        actionBar->addWidget(m_contextToggleBtn);
        actionBar->addWidget(m_regenSummaryBtn);
        sumWrap->addLayout(actionBar);

        // --- összecsukható kontextus-doboz (alapból rejtve; a ⚙ gomb hajtja ki) ---
        m_contextPanel = new QWidget(sumTab);
        auto* ctxLay = new QHBoxLayout(m_contextPanel);
        ctxLay->setContentsMargins(0, 0, 0, 0);
        m_summaryContextEdit = new QPlainTextEdit(m_contextPanel);
        m_summaryContextEdit->setPlaceholderText(QStringLiteral(
            "Miről szólt? (téma, nevek, szakszavak…) — a pontosabb összefoglalóhoz"));
        m_summaryContextEdit->setMaximumHeight(56);
        ctxLay->addWidget(m_summaryContextEdit);
        m_contextPanel->setVisible(false);
        sumWrap->addWidget(m_contextPanel);
        connect(m_contextToggleBtn, &QPushButton::toggled, m_contextPanel, &QWidget::setVisible);

        // --- tartalom-stack: üres hint / kész összefoglaló / téma-munkaterület ---
        m_summaryStack = new QStackedWidget(sumTab);
        m_summaryStack->addWidget(m_summaryView);       // page 0: kész összefoglaló

        m_summaryEmptyPage = new QWidget(m_summaryStack);
        auto* el = new QVBoxLayout(m_summaryEmptyPage);
        el->addStretch(1);
        auto* emptyLbl = new QLabel(QStringLiteral(
            "Még nincs összefoglaló.\nVálassz fent: „Gyors összefoglaló” egy lépésben, "
            "vagy „Témánként” a részletes, szerkeszthető elemzéshez."), m_summaryEmptyPage);
        emptyLbl->setAlignment(Qt::AlignCenter);
        emptyLbl->setWordWrap(true);
        makeHintLabel(emptyLbl);
        el->addWidget(emptyLbl);
        el->addStretch(1);
        m_summaryStack->addWidget(m_summaryEmptyPage);  // page 1: üres hint

        // page 2: téma-munkaterület (kigyűjtött témák — szerkeszthető cím/gist + per-téma elemzés).
        m_topicEditorPage = new QWidget(m_summaryStack);
        auto* tl = new QVBoxLayout(m_topicEditorPage);
        tl->setContentsMargins(0, 0, 0, 0);
        auto* topRow = new QHBoxLayout();
        m_backToSummaryBtn = new QPushButton(QStringLiteral("‹  Vissza az összefoglalóhoz"),
                                             m_topicEditorPage);
        m_backToSummaryBtn->setFlat(true);
        m_backToSummaryBtn->setVisible(false);
        auto* tHint = new QLabel(QStringLiteral(
            "Szerkeszd a témákat (cím + gist), majd „Elemzés indítása”. Minden elemzés azonnal "
            "mentődik; a ▶/↻ gombbal témánként is futtatható."), m_topicEditorPage);
        tHint->setWordWrap(true);
        makeHintLabel(tHint);
        topRow->addWidget(m_backToSummaryBtn, 0);
        topRow->addWidget(tHint, 1);
        tl->addLayout(topRow);
        connect(m_backToSummaryBtn, &QPushButton::clicked, this, [this]() {
            m_topicEditorActive = false;
            bool ok = false; const tanara::Meeting m = selectedMeeting(&ok);
            if (ok) reloadSummaryView(m);
        });

        auto* tScroll = new QScrollArea(m_topicEditorPage);
        tScroll->setWidgetResizable(true);
        tScroll->setFrameShape(QFrame::NoFrame);
        auto* tRowsHost = new QWidget(tScroll);
        m_topicRowsLayout = new QVBoxLayout(tRowsHost);
        m_topicRowsLayout->setContentsMargins(0, 0, 0, 0);
        m_topicRowsLayout->addStretch(1);   // a sorok e fölé kerülnek (insertWidget)
        tScroll->setWidget(tRowsHost);
        tl->addWidget(tScroll, 1);

        auto* actRow = new QHBoxLayout();
        auto* addTopicBtn = new QPushButton(QStringLiteral("➕  Új téma"), m_topicEditorPage);
        connect(addTopicBtn, &QPushButton::clicked, this, [this]() {
            addTopicRow(tanara::SummaryTopic{});   // üres sor (id-t a mentéskor kap, ha kell)
        });
        m_startAnalysisBtn = new QPushButton(QStringLiteral("Elemzés indítása →"), m_topicEditorPage);
        m_startAnalysisBtn->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; }"));
        m_startAnalysisBtn->setToolTip(QStringLiteral(
            "A hiányzó témák elemzése lefut, majd elkészül a végső vezetői összefoglaló."));
        actRow->addWidget(addTopicBtn, 0);
        actRow->addStretch(1);
        actRow->addWidget(m_startAnalysisBtn, 0);
        tl->addLayout(actRow);
        connect(m_startAnalysisBtn, &QPushButton::clicked, this, &MainWindow::onStartAnalysis);
        m_summaryStack->addWidget(m_topicEditorPage);   // page 2

        sumWrap->addWidget(m_summaryStack, 1);

        connect(m_generateSummaryBtn, &QPushButton::clicked, this, &MainWindow::onShowOrGenerateSummary);
        connect(m_regenSummaryBtn, &QPushButton::clicked, this, &MainWindow::onSummarizeClicked);
        connect(m_complexBtn, &QPushButton::clicked, this, &MainWindow::onComplexClicked);

        // A Sávok-fül elé szúrjuk vissza (Átirat | Összefoglaló | Sávok sorrend).
        const int tracksIdx = m_tabs->indexOf(m_tracksPanel);
        if (tracksIdx >= 0)
            m_tabs->insertTab(tracksIdx, sumTab, summaryTitle);
        else
            m_tabs->addTab(sumTab, summaryTitle);
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

    // Lekeverés determinisztikus folyamatjelzője (nem-modális, az állapotsorban). A
    // háttér-lekeverés (auto vagy kézi) ezt mozgatja a mixdownProgress jelből; nem blokkol,
    // közben akár új felvétel is indítható.
    m_convertBar = new QProgressBar(this);
    m_convertBar->setRange(0, 100);
    m_convertBar->setMaximumWidth(200);
    m_convertBar->setFormat(QStringLiteral("🎧 Lekeverés %p%"));
    m_convertBar->setVisible(false);
    statusBar()->addPermanentWidget(m_convertBar);

    // --- felső sáv akciói (a meglévő logikát hívják) ---
    connect(m_newRecordingBtn, &QPushButton::clicked, this, &MainWindow::popOutRecorder);
    connect(m_peopleBtn, &QPushButton::clicked, this, &MainWindow::openPeopleManager);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    // --- jobb pane: beszélők-sáv + kapu-panel + összefoglaló-generálás ---
    // (A Sávok a fülön + a Nézet→Sávok menüből érhető el; a régi „Sávok…" fejléc-gomb
    //  megszűnt, redundáns volt.)
    connect(m_speakersEditBtn, &QPushButton::clicked, this, &MainWindow::onIdentifyParticipants);
    connect(m_transcribeBtn, &QPushButton::clicked, this, &MainWindow::onTranscribeClicked);
    // (a Gyors összefoglaló gombja már az Összefoglaló-fül akció-sávjában kötve — lásd buildUi)

    // Üres induló állapot (nincs kiválasztott meeting).
    m_titleLabel->clear();
    m_metaLabel->clear();
    m_speakersBar->setVisible(false);
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
    connect(participantsAct, &QAction::triggered, this, &MainWindow::onIdentifyParticipants);
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
    // A kontextus-doboz a meetinghez mentett leírást tükrözi (átirat után is szerkeszthető).
    if (m_summaryContextEdit && m_summaryContextEdit->toPlainText() != m.contextNote) {
        const QSignalBlocker block(m_summaryContextEdit);
        m_summaryContextEdit->setPlainText(m.contextNote);
    }

    // Amíg a téma-munkaterület aktív (2. kör fut / vár), a köztes reload-ok (setBusy(false),
    // store-jelek) ne rántsák el a lapot a szerkesztőről.
    if (m_topicEditorActive && m.id == m_topicsMeetingId) {
        m_summaryStack->setCurrentWidget(m_topicEditorPage);
        updateSummaryActionBar(m);
        return;
    }

    const QString path = QDir(m.folder).filePath(QStringLiteral("summary.md"));
    const QString md = readMarkdownFile(path);
    if (md.isEmpty()) {
        m_summaryStack->setCurrentWidget(m_summaryEmptyPage);
    } else {
        m_summaryView->setMarkdown(md);
        m_summaryStack->setCurrentWidget(m_summaryView);
    }
    updateSummaryActionBar(m);
}

// Az akció-sáv gombjainak kapuzása/láthatósága a meeting állapota szerint:
//  - Gyors / Témánként: engedélyezve, ha az összefoglaló futtatható (canRun Summarize);
//  - Újragenerálás: csak akkor látszik, ha VAN már kész gyors/komplex összefoglaló;
//  - a nem-futtatható ok tooltipbe kerül (a gomb megnyitja a beállításokat, ha az a blokkoló).
void MainWindow::updateSummaryActionBar(const tanara::Meeting& m) {
    if (!m_controller) return;
    const tanara::ReadinessResult rs = m_controller->canRun(tanara::WorkflowStep::Summarize, m.id);
    const bool hasSummary = !readMarkdownFile(
        QDir(m.folder).filePath(QStringLiteral("summary.md"))).isEmpty();
    const bool inWorkspace = (m_summaryStack->currentWidget() == m_topicEditorPage);

    // Provider-konfig/auth blokknál a gomb ENGEDÉLYEZVE marad, de a Beállításokat nyitja
    // (az onSummarizeClicked/onComplexClicked kapuz) — a többi blokknál (nincs átirat) tiltva.
    const bool cfgBlock = !rs.runnable
        && (rs.blockerKind == tanara::BlockerKind::ProviderConfig
            || rs.blockerKind == tanara::BlockerKind::Auth);
    const bool actionsEnabled = rs.runnable || cfgBlock;
    if (m_generateSummaryBtn) {
        m_generateSummaryBtn->setEnabled(actionsEnabled);
        m_generateSummaryBtn->setToolTip(rs.runnable
            ? QStringLiteral("Egy lépésben, egy modell-hívással készít vezetői összefoglalót + teendőket.")
            : (cfgBlock ? QStringLiteral("Beállítás szükséges: %1").arg(rs.detail)
                        : QStringLiteral("Nem futtatható: %1").arg(rs.detail)));
    }
    if (m_complexBtn) m_complexBtn->setEnabled(actionsEnabled);
    // Az Újragenerálás a kész gyors-összefoglalóra vonatkozik; a téma-munkaterületen elrejtjük.
    if (m_regenSummaryBtn) m_regenSummaryBtn->setVisible(hasSummary && !inWorkspace);
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
    // Statikus összegző: a hozzárendelt nevek + ismeretlenek darabszáma (a per-beszélő
    // átnevezés/teszt az Átirat-fül legendájában él — itt csak gyors áttekintés).
    // A nyers beszélő-címkék a speakerMap kulcsai; a valódi nevek az értékei.
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
    m_speakersSummary->setText(parts.isEmpty()
        ? QStringLiteral("még nincs azonosítva — a neveket az Átiraton add meg")
        : parts.join(QStringLiteral(" · ")));

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

        // Lekeverés-gomb (kézi mód / discoverability): csak ha még nincs kevert fájl ÉS
        // nem fut épp a lekeverése. Auto módban jellemzően már fut/kész, így rejtve marad.
        if (m_convertBtn)
            m_convertBtn->setVisible(m.mixdownFile.isEmpty()
                                     && !m_converting.contains(m.id));

        // A kontextus-doboz feltöltése a meetinghez mentett leírással (re-átírásnál megmarad).
        if (m_contextEdit && m_contextEdit->toPlainText() != m.contextNote) {
            const QSignalBlocker block(m_contextEdit);
            m_contextEdit->setPlainText(m.contextNote);
        }

        // Résztvevők-sor: a lefuttatott azonosítás eredménye (session-cache), különben a
        // sáv-címkékből az eddig tudott névsor + az ismeretlen (távoli) oldalak száma.
        if (m_participantsResult) {
            QString line;
            const QString cached = m_participantsCache.value(m.id);
            if (!cached.isEmpty()) {
                line = QStringLiteral("🔎 Résztvevők: %1").arg(cached);
            } else {
                QStringList named;
                int unknown = 0;
                for (const tanara::Track& t : m.tracks) {
                    if (!t.active) continue;
                    const QString lbl = t.speakerLabel.trimmed();
                    if (t.kind == tanara::TrackKind::Mic && !lbl.isEmpty()
                        && lbl != QStringLiteral("Rendszer")) {
                        if (!named.contains(lbl)) named << lbl;
                    } else if (t.kind == tanara::TrackKind::Loopback) {
                        ++unknown;   // távoli oldal — a nevek még ismeretlenek
                    }
                }
                if (!named.isEmpty() || unknown > 0)
                    line = QStringLiteral("Résztvevők: %1")
                               .arg(participantsSummary(named, unknown, named.size() + unknown));
            }
            m_participantsResult->setText(line);
            m_participantsResult->setVisible(!line.isEmpty());
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

    // Van átirat → fülek. Az Összefoglaló-fül akció-sávját külön kezeli az
    // updateSummaryActionBar (a reloadSummaryView hívja) — itt csak a lapra váltunk.
    m_reviewStack->setCurrentWidget(ui->tabsPage);
    updateSummaryActionBar(m);
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
        m_transcribeBtn->setEnabled(false);
        m_generateSummaryBtn->setEnabled(false);
        if (m_identifyParticipantsBtn) m_identifyParticipantsBtn->setEnabled(false);
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        return;
    }

    m_currentMeetingId = m.id;
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

    // Context-envelope: a State A doboz tartalmát (miről szólt a meeting) elmentjük →
    // a STT (Soniox „context") és az összefoglaló is megkapja. Nincs külön popup.
    if (m_contextEdit)
        m_controller->setMeetingContextNote(m.id, m_contextEdit->toPlainText());

    setBusy(true, QStringLiteral("Átírás indítása…"));
    m_controller->transcribeMeeting(m.id);
}

void MainWindow::onShowOrGenerateSummary() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    // Fül-szerű viselkedés: ha MÁR van kész gyors/komplex összefoglaló, csak megjelenítjük —
    // NEM generálunk újra (arra a ↻ Újragenerálás való). Ha nincs, indítjuk a generálást.
    const QString md = readMarkdownFile(QDir(m.folder).filePath(QStringLiteral("summary.md")));
    if (!md.isEmpty()) {
        m_topicEditorActive = false;   // ha épp a téma-munkaterületen voltunk, kilépünk
        reloadSummaryView(m);
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        m_tabs->setCurrentWidget(m_summaryTab);
        return;
    }
    onSummarizeClicked();
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
    // A summary-fül fejlécében (átirat után) finomított kontextust elmentjük, mielőtt
    // (újra)generálunk — így a modell a frissített leírást kapja.
    if (m_summaryContextEdit)
        m_controller->setMeetingContextNote(m.id, m_summaryContextEdit->toPlainText().trimmed());
    setBusy(true, QStringLiteral("Összefoglaló készítése…"));
    m_controller->summarizeMeeting(m.id);
}

void MainWindow::onComplexClicked() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;
    const tanara::ReadinessResult rs =
        m_controller->canRun(tanara::WorkflowStep::Summarize, m.id);
    if (!rs.runnable) {
        if (rs.blockerKind == tanara::BlockerKind::ProviderConfig
            || rs.blockerKind == tanara::BlockerKind::Auth)
            openSettings();
        else
            statusBar()->showMessage(QStringLiteral("Nem indítható: ") + rs.detail, 6000);
        return;
    }
    if (m_summaryContextEdit)
        m_controller->setMeetingContextNote(m.id, m_summaryContextEdit->toPlainText().trimmed());
    setBusy(true, QStringLiteral("Témák kigyűjtése…"));
    m_controller->extractMeetingTopics(m.id);   // → topicsReady → onTopicsReady (szerkesztő)
}

void MainWindow::onTopicsReady(QString meetingId, QVector<tanara::SummaryTopic> topics) {
    setBusy(false);
    if (meetingId != m_currentMeetingId)
        return;
    m_topicsMeetingId = meetingId;
    clearTopicRows();
    for (const tanara::SummaryTopic& t : topics)
        addTopicRow(t);
    if (topics.isEmpty())
        addTopicRow(tanara::SummaryTopic{});   // legalább egy üres sor a szerkesztéshez
    // A már lemezen lévő (korábbi futásból megőrzött) elemzések kártyái „✓ Kész"-t kapnak,
    // a törzsük lenyitható — a batch ezeket kihagyja, a ↻ gombbal egyenként újrafuttathatók.
    if (m_controller)
        for (const tanara::TopicAnalysis& a : m_controller->topicAnalyses(meetingId))
            if (TopicRow* r = topicRowById(a.topicId)) {
                setTopicRowState(*r, TopicState::Done);
                if (r->result)      r->result->setMarkdown(a.renderMarkdown());
                if (r->resultBlock) r->resultBlock->setVisible(true);
            }
    if (m_startAnalysisBtn) m_startAnalysisBtn->setEnabled(true);
    // A „Vissza az összefoglalóhoz" csak akkor kell, ha van már kész (gyors/komplex) summary.
    if (m_backToSummaryBtn) {
        bool ok = false; const tanara::Meeting mm = selectedMeeting(&ok);
        const bool hasSummary = ok && !readMarkdownFile(
            QDir(mm.folder).filePath(QStringLiteral("summary.md"))).isEmpty();
        m_backToSummaryBtn->setVisible(hasSummary);
    }
    // A téma-munkaterületre váltunk (a setBusy(false) az imént a kész/üres lapra állította),
    // és ott is tartjuk a köztes reload-ok alatt (lásd reloadSummaryView).
    m_topicEditorActive = true;
    m_reviewStack->setCurrentWidget(ui->tabsPage);
    m_tabs->setCurrentWidget(m_summaryTab);
    m_summaryStack->setCurrentWidget(m_topicEditorPage);
    bool ok2 = false; const tanara::Meeting mm2 = selectedMeeting(&ok2);
    if (ok2) updateSummaryActionBar(mm2);   // Újragenerálás elrejtése a munkaterületen
}

void MainWindow::addTopicRow(const tanara::SummaryTopic& t) {
    if (!m_topicRowsLayout)
        return;
    // Kompakt kártya: egy soros FEJLÉC (állapot-pötty · cím · státusz · futtat/kinyit/törlés),
    // alatta egy vékony progress, és EGY összecsukható RÉSZLETEK-panel (gist-szerkesztő + a
    // kész elemzés törzse). Így 5-10 téma is átlátható marad — a kész témák csukott csíkok.
    auto* card = new QFrame(m_topicEditorPage);
    card->setFrameShape(QFrame::StyledPanel);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(8, 6, 8, 6);
    v->setSpacing(4);

    // --- fejléc-sor (glyph-mentes vezérlők: CSS-pötty, stílusnyíl, téma-ikonok) ---
    auto* head = new QHBoxLayout();
    head->setSpacing(6);
    auto* dot = new QLabel(card);
    dot->setFixedSize(10, 10);       // CSS-rajzolt kör; a színt a setTopicRowState állítja
    dot->setToolTip(QStringLiteral("Állapot"));
    auto* title = new QLineEdit(t.title, card);
    title->setPlaceholderText(QStringLiteral("Téma címe"));
    QFont tf = title->font(); tf.setBold(true); title->setFont(tf);
    title->setFrame(false);   // tisztább fejléc — a cím inline szerkeszthető, keret nélkül
    auto* status = new QLabel(card);
    status->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    auto* run = new QPushButton(card);
    run->setFixedWidth(32);
    run->setToolTip(QStringLiteral("Ennek a témának az elemzése (a kész eredmény mentődik)"));
    auto* expand = new QToolButton(card);
    expand->setArrowType(Qt::RightArrow);
    expand->setAutoRaise(true); expand->setCheckable(true);
    expand->setToolTip(QStringLiteral("Részletek: leírás + elemzés"));
    head->addWidget(dot, 0);
    head->addWidget(title, 1);
    head->addWidget(status, 0);
    head->addWidget(run, 0);
    head->addWidget(expand, 0);
    v->addLayout(head);

    // --- vékony busy-progress (csak elemzés közben) ---
    auto* prog = new QProgressBar(card);
    prog->setRange(0, 0);            // indeterminate — az épp elemzett téma „dolgozik" jelzése
    prog->setTextVisible(false);
    prog->setFixedHeight(3);
    prog->setVisible(false);
    v->addWidget(prog);

    // --- lenyíló részletek-panel: gist-szerkesztő + (kész esetén) az elemzés eredménye ---
    auto* details = new QWidget(card);
    auto* dv = new QVBoxLayout(details);
    dv->setContentsMargins(0, 2, 0, 0);
    dv->setSpacing(3);
    auto* gistLbl = new QLabel(QStringLiteral("Rövid leírás a modellnek (opcionális):"), details);
    makeHintLabel(gistLbl, /*small*/ true);
    auto* summary = new QPlainTextEdit(t.summary, details);
    summary->setPlaceholderText(QStringLiteral("Miről szól ez a téma — 1-2 mondat"));
    summary->setTabChangesFocus(true);
    summary->setFixedHeight(52);
    dv->addWidget(gistLbl);
    dv->addWidget(summary);

    auto* resultBlock = new QWidget(details);
    auto* rv = new QVBoxLayout(resultBlock);
    rv->setContentsMargins(0, 4, 0, 0);
    rv->setSpacing(3);
    auto* resLbl = new QLabel(QStringLiteral("Elemzés eredménye:"), resultBlock);
    makeHintLabel(resLbl, /*small*/ true);
    auto* result = new QTextBrowser(resultBlock);
    result->setOpenExternalLinks(true);
    // Fix kezdő magasság (a húzható fogantyú állítja) — nem görgető 260px-be szorítva.
    result->setMinimumHeight(180);
    result->setMaximumHeight(180);
    rv->addWidget(resLbl);
    rv->addWidget(result);
    rv->addWidget(new HeightGrip(result, /*minH*/ 90, resultBlock));   // húzható méretező-gutter
    resultBlock->setVisible(false);   // csak kész elemzésnél
    dv->addWidget(resultBlock);

    // A törlés a részletek-panel alján (nem a fejlécben) — szándékos, nehezebben elvéthető,
    // és megerősítést kér, mert a téma + a kész elemzése is véglegesen elvész.
    auto* delRow = new QHBoxLayout();
    auto* del = new QPushButton(QStringLiteral("Téma törlése"), details);
    del->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    del->setStyleSheet(QStringLiteral("QPushButton { color: #d64545; }"));
    delRow->addStretch(1);
    delRow->addWidget(del, 0);
    dv->addLayout(delRow);

    details->setVisible(false);
    v->addWidget(details);

    // A kártyát a záró stretch ELÉ szúrjuk (az utolsó elem a stretch).
    m_topicRowsLayout->insertWidget(m_topicRowsLayout->count() - 1, card);

    TopicRow tr{ t.id, card, dot, title, summary, prog, status, run, expand, details, resultBlock, result };
    m_topicRows.append(tr);
    // Új/üres téma alapból NYITVA (hogy szerkeszd); betöltött téma csukva marad (kompakt lista).
    setTopicRowState(m_topicRows.last(), TopicState::Draft);
    setTopicRowExpanded(m_topicRows.last(), t.title.trimmed().isEmpty());

    connect(expand, &QPushButton::toggled, this, [this, card](bool on) {
        if (TopicRow* r = topicRowByCard(card)) setTopicRowExpanded(*r, on);
    });
    connect(del, &QPushButton::clicked, this, [this, card]() {
        TopicRow* r = topicRowByCard(card);
        const QString name = (r && r->title) ? r->title->text().trimmed() : QString();
        const auto btn = QMessageBox::question(this, QStringLiteral("Téma törlése"),
            name.isEmpty()
                ? QStringLiteral("Biztosan törlöd ezt a témát? A kész elemzése is elvész.")
                : QStringLiteral("Biztosan törlöd a(z) „%1” témát? A kész elemzése is elvész.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes)
            return;
        for (int i = 0; i < m_topicRows.size(); ++i)
            if (m_topicRows[i].row == card) { m_topicRows.removeAt(i); break; }
        card->deleteLater();
    });
    connect(run, &QPushButton::clicked, this, [this, card]() {
        if (!m_controller || m_topicsMeetingId.isEmpty())
            return;
        TopicRow* r = topicRowByCard(card);
        if (!r) return;
        const QString title = r->title ? r->title->text().trimmed() : QString();
        if (title.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("A témához cím kell az elemzéshez."), 5000);
            return;
        }
        tanara::SummaryTopic t;
        if (r->id.isEmpty())
            r->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        t.id = r->id;
        t.title = title;
        t.summary = r->summary ? r->summary->toPlainText().trimmed() : QString();
        m_controller->analyzeTopic(m_topicsMeetingId, t);   // → queued/started/ready/failed
    });
}

// A kártya vizuális állapota egy helyen: pötty-szín + státusz-szöveg + futtat-ikon + progress.
// A státusz-szöveg elején egy ASCII-jelző (font-független), a run beépített téma-ikon.
void MainWindow::setTopicRowState(TopicRow& r, TopicState st, const QString& tip) {
    struct V { const char* color; const char* text; bool reload; bool busy; bool runEnabled; };
    V vis;
    switch (st) {
        case TopicState::Draft:   vis = {"#9aa0a6", "",         false, false, true}; break;
        case TopicState::Queued:  vis = {"#c99a00", "Sorban",   false, false, false}; break;
        case TopicState::Running: vis = {"#2d7ff9", "Elemzés…", false, true,  false}; break;
        case TopicState::Done:    vis = {"#2fa84f", "Kész",     true,  false, true}; break;
        case TopicState::Failed:  vis = {"#d64545", "Hiba",     true,  false, true}; break;
    }
    // FIGYELEM: a vis.text UTF-8 (ékezetes) — QString::fromUtf8 kell, a QLatin1String mojibake-t ad.
    const QString stText = QString::fromUtf8(vis.text);
    if (r.dot) {
        r.dot->setStyleSheet(QStringLiteral(
            "background-color: %1; border-radius: 5px;").arg(QLatin1String(vis.color)));
        r.dot->setToolTip(tip.isEmpty() ? stText : tip);
    }
    if (r.status) { r.status->setText(stText);
                    r.status->setVisible(!stText.isEmpty());
                    r.status->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(QLatin1String(vis.color)));
                    r.status->setToolTip(tip); }
    if (r.run)    { r.run->setIcon(style()->standardIcon(
                        vis.reload ? QStyle::SP_BrowserReload : QStyle::SP_MediaPlay));
                    r.run->setEnabled(vis.runEnabled); }
    if (r.prog)   r.prog->setVisible(vis.busy);
}

void MainWindow::setTopicRowExpanded(TopicRow& r, bool on) {
    if (r.details) r.details->setVisible(on);
    if (r.expand) {
        const QSignalBlocker block(r.expand);
        r.expand->setChecked(on);
        r.expand->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    }
}

void MainWindow::clearTopicRows() {
    for (const TopicRow& r : m_topicRows)
        if (r.row) r.row->deleteLater();
    m_topicRows.clear();
}

MainWindow::TopicRow* MainWindow::topicRowById(const QString& topicId) {
    for (TopicRow& r : m_topicRows)
        if (r.id == topicId)
            return &r;
    return nullptr;
}

MainWindow::TopicRow* MainWindow::topicRowByCard(const QWidget* card) {
    for (TopicRow& r : m_topicRows)
        if (r.row == card)
            return &r;
    return nullptr;
}

// A 2. kör per-téma életciklusa a kártyákon: sorban áll → elemzés (busy bar) → ✓ kész
// (perzisztálva) / ⚠ hiba. Egy téma bukása a többit nem érinti; a ↻ gombbal újrafuttatható.
void MainWindow::onTopicQueued(QString meetingId, QString topicId) {
    if (meetingId != m_topicsMeetingId) return;
    if (TopicRow* r = topicRowById(topicId))
        setTopicRowState(*r, TopicState::Queued);
}

void MainWindow::onTopicStarted(QString meetingId, QString topicId) {
    if (meetingId != m_topicsMeetingId) return;
    if (TopicRow* r = topicRowById(topicId))
        setTopicRowState(*r, TopicState::Running);
}

void MainWindow::onTopicReady(QString meetingId, tanara::TopicAnalysis analysis) {
    if (meetingId != m_topicsMeetingId) return;
    if (TopicRow* r = topicRowById(analysis.topicId)) {
        setTopicRowState(*r, TopicState::Done);
        if (r->result)      r->result->setMarkdown(analysis.renderMarkdown());
        if (r->resultBlock) r->resultBlock->setVisible(true);
    }
}

void MainWindow::onTopicFailed(QString meetingId, QString topicId, QString error) {
    if (meetingId != m_topicsMeetingId) return;
    if (TopicRow* r = topicRowById(topicId))
        setTopicRowState(*r, TopicState::Failed, error);
    statusBar()->showMessage(QStringLiteral("Téma-elemzés hiba: %1").arg(error), 8000);
}

void MainWindow::onTopicQueueFinished(QString meetingId, int okCount, int failCount) {
    if (meetingId != m_topicsMeetingId) return;
    if (m_startAnalysisBtn) m_startAnalysisBtn->setEnabled(true);
    // Ha batch után reduce következik, a controller rögtön jobProgress-t ad (busy vissza);
    // egyedi (kártyás) futás vagy hibás batch után itt áll le a busy.
    setBusy(false);
    if (failCount == 0) {
        statusBar()->showMessage(QStringLiteral("%1 téma elemzése kész.").arg(okCount), 5000);
    } else {
        statusBar()->showMessage(QStringLiteral("%1 téma kész, %2 hibázott — a hibásak a kártyájukon "
                                                "újrafuttathatók.").arg(okCount).arg(failCount), 10000);
    }
}

void MainWindow::onStartAnalysis() {
    if (!m_controller || m_topicsMeetingId.isEmpty())
        return;
    QVector<tanara::SummaryTopic> topics;
    for (TopicRow& r : m_topicRows) {
        const QString title = r.title ? r.title->text().trimmed() : QString();
        if (title.isEmpty())
            continue;   // üres című sorokat kihagyjuk
        tanara::SummaryTopic t;
        t.id = r.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : r.id;
        r.id = t.id;   // az id-t visszaírjuk, hogy a per-téma állapot a helyes kártyára illeszkedjen
        t.title = title;
        t.summary = r.summary ? r.summary->toPlainText().trimmed() : QString();
        topics.append(t);
    }
    if (topics.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Adj meg legalább egy témát."), 5000);
        return;
    }
    if (m_startAnalysisBtn) m_startAnalysisBtn->setEnabled(false);
    setBusy(true, QStringLiteral("Témánkénti elemzés…"));
    // Csak a még elemzetlen témák futnak; ha mind kész, egyből a reduce jön → summaryReady.
    m_controller->generateComplexSummary(m_topicsMeetingId, topics);
}

// Emberi összegző mondat: nincs név → „N különböző partner azonosítva";
// van név → „A, B és X ismeretlen partner" (X==0 → csak a nevek).
static QString participantsSummary(QStringList named, int unknownCount, int totalDistinct) {
    named.removeDuplicates();
    if (named.isEmpty())
        return QStringLiteral("%1 különböző partner azonosítva").arg(totalDistinct);
    QString s = named.join(QStringLiteral(", "));
    if (unknownCount > 0)
        s += QStringLiteral(" és %1 ismeretlen partner").arg(unknownCount);
    return s;
}

void MainWindow::onIdentifyParticipants() {
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (!ok || !m_controller)
        return;

    // Megszakítható progress: a számítás a fő szálon fut (a store-mutáció miatt), de a
    // callback minden lépésnél frissíti a dialógust + processEvents-szel életben tartja
    // a UI-t és figyeli a „Megszakítás"-t. A core UI-mentes marad.
    QProgressDialog dlg(QStringLiteral("Résztvevők azonosítása a hang alapján…"),
                        QStringLiteral("Megszakítás"), 0, 0, this);
    dlg.setWindowTitle(QStringLiteral("Résztvevők azonosítása"));
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.setValue(0);
    auto progress = [&dlg](int done, int total) -> bool {
        if (total > 0) { dlg.setMaximum(total); dlg.setValue(done); }
        QCoreApplication::processEvents();
        return !dlg.wasCanceled();
    };

    QString summary;
    if (m.hasTranscript) {
        // Van átirat → a biztos DB-találatokat BEÍRJUK a speakerMap-be; a visszajelzés a
        // (tartós) Beszélők-sáv frissülése.
        m_controller->autoIdentifyMeeting(m.id, progress);
        if (dlg.wasCanceled()) {
            statusBar()->showMessage(QStringLiteral("Azonosítás megszakítva."), 4000);
            return;
        }
        const tanara::Meeting fresh = m_controller->store()->load(m.id);
        QStringList named;
        int unknown = 0;
        for (auto it = fresh.speakerMap.constBegin(); it != fresh.speakerMap.constEnd(); ++it) {
            if (it.value().trimmed().isEmpty()) ++unknown;
            else named << it.value().trimmed();
        }
        summary = participantsSummary(named, unknown, fresh.speakerMap.size());
        reloadSpeakersBar(fresh);
    } else {
        // Nincs átirat → ELŐNÉZET: hang-klaszterek + DB-találat. A nyers címkék csak az
        // átírással keletkeznek, ezért itt nem írunk be — a tartós visszajelzés a State A
        // panel eredmény-sora (+ session-cache), nem egy eltűnő popup.
        const auto guesses = m_controller->identifyParticipants(m.id, progress);
        if (dlg.wasCanceled()) {
            statusBar()->showMessage(QStringLiteral("Azonosítás megszakítva."), 4000);
            return;
        }
        QStringList named;
        int unknown = 0;
        for (const auto& g : guesses) {
            if (g.name.trimmed().isEmpty()) ++unknown;
            else named << g.name.trimmed();
        }
        summary = participantsSummary(named, unknown, guesses.size());
        m_participantsCache.insert(m.id, summary);
        if (m_participantsResult) {
            m_participantsResult->setText(QStringLiteral("🔎 Résztvevők: %1").arg(summary));
            m_participantsResult->setVisible(true);
        }
    }
    dlg.reset();
    statusBar()->showMessage(summary, 6000);
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
    if (meetingId == m_topicsMeetingId)
        m_topicEditorActive = false;   // a kész összefoglaló lapja jöhet a szerkesztő helyére
    setBusy(false);
    statusBar()->showMessage(QStringLiteral("Összefoglaló elkészült."), 5000);
    if (meetingId != m_currentMeetingId)
        return;
    bool ok = false;
    const tanara::Meeting m = selectedMeeting(&ok);
    if (ok) {
        reloadSummaryView(m);                       // → a stack a kész összefoglalóra vált
        m_reviewStack->setCurrentWidget(ui->tabsPage);
        m_tabs->setCurrentWidget(m_summaryTab);     // az Összefoglaló-fül (fejléc + stack)
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
        m_recordBar->refreshFromSettings();   // a Beállítások közben változhattak
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
    m_recordBar->refreshFromSettings();   // a Beállításokban megadott eszköz-policy tükrözése
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
    if (dlg.exec() == QDialog::Accepted && m_recordBar)
        m_recordBar->refreshFromSettings();   // a felvevő tükrözze az új eszköz-policyt
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
