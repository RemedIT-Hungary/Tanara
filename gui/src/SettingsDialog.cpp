#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/SummaryService.h"
#include "tanara/ComplexSummaryService.h"
#include "tanara/Types.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/audio/DeviceManager.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QLabel>
#include <QFileDialog>
#include <QSizePolicy>
#include <QFont>
#include <QPalette>
#include <QDir>
#include <QVariant>

namespace tanara_gui {

// Az összefoglaló-prompt id-jához tartozó beépített default (egyetlen igazságforrás a
// betöltés/reset/mentés számára). id: "simple" | "topic" | "analysis".
static QString summaryPromptDefault(const QString& id) {
    if (id == QStringLiteral("topic"))    return tanara::ComplexSummaryService::defaultTopicPrompt();
    if (id == QStringLiteral("analysis")) return tanara::ComplexSummaryService::defaultAnalysisPrompt();
    return tanara::SummaryService::defaultSystemPrompt();   // "simple"
}

using tanara::ConfigField;
using tanara::ConfigFieldType;
using tanara::ConfigOption;
using tanara::ProviderConfig;
using tanara::ProviderDescriptor;
using tanara::ProviderKind;

namespace {

// A ProviderConfig jól-ismert mezői (a többi → extra).
bool isWellKnownKey(const QString& key) {
    return key == QLatin1String("baseUrl") || key == QLatin1String("model")
        || key == QLatin1String("temperature") || key == QLatin1String("maxTokens");
}

// Egy jól-ismert mező aktuális értéke string-formában (a load-hoz).
QString wellKnownValue(const ProviderConfig& cfg, const QString& key) {
    if (key == QLatin1String("baseUrl")) return cfg.baseUrl;
    if (key == QLatin1String("model")) return cfg.model;
    if (key == QLatin1String("temperature")) return QString::number(cfg.temperature);
    if (key == QLatin1String("maxTokens")) return QString::number(cfg.maxTokens);
    return QString();
}

} // namespace

SettingsDialog::SettingsDialog(tanara::AppController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Beállítások"));
    setModal(true);

    auto* root = new QVBoxLayout(this);

    // Segéd: könyvtár-mező + "Tallózás…" gomb egy sorba.
    auto makeDirRow = [this](QWidget* parentWidget, QLineEdit*& field,
                             const QString& caption) -> QWidget* {
        auto* container = new QWidget(parentWidget);
        auto* h = new QHBoxLayout(container);
        h->setContentsMargins(0, 0, 0, 0);
        field = new QLineEdit(container);
        auto* browse = new QPushButton(QStringLiteral("Tallózás…"), container);
        h->addWidget(field, 1);
        h->addWidget(browse, 0);
        wireFolderPicker(field, browse, caption);
        return container;
    };

    const tanara::AppSettings s =
        (m_controller && m_controller->settings())
            ? m_controller->settings()->settings()
            : tanara::AppSettings{};

    // Másodlagos („muted") szöveg: a PlaceholderText szerep téma-helyes ÉS olvasható
    // (a palette(mid) sötét témán túl sötét → olvashatatlan). Stylesheet helyett
    // foregroundRole, hogy a paletta-szín érvényesüljön; kicsit kisebb betű.
    auto applyMuted = [](QLabel* l) {
        l->setForegroundRole(QPalette::PlaceholderText);
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        l->setFont(f);
    };

    auto* tabs = new QTabWidget(this);

    // ============================ Fül 1: Általános ============================
    // „ki vagy + hova ment" — ritkán nyúlsz hozzá.
    auto* generalPage = new QWidget(this);
    auto* gl = new QVBoxLayout(generalPage);
    auto* idBox = new QGroupBox(QStringLiteral("Azonosítás"), generalPage);
    auto* idForm = new QFormLayout(idBox);
    m_userSpeakerName = new QLineEdit(idBox);
    idForm->addRow(QStringLiteral("Saját beszélő neve:"), m_userSpeakerName);
    gl->addWidget(idBox);

    auto* dirsBox = new QGroupBox(QStringLiteral("Mappák"), generalPage);
    auto* dirsForm = new QFormLayout(dirsBox);
    // A címke a mező FÖLÉ, külön sorba — a hosszú út + „Tallózás…" gombbal így nem zsúfolt.
    dirsForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    dirsForm->addRow(QStringLiteral("Felvételek mappája:"),
                     makeDirRow(dirsBox, m_audioDir, QStringLiteral("Felvételek mappája")));
    dirsForm->addRow(QStringLiteral("Jegyzetek mappája:"),
                     makeDirRow(dirsBox, m_notesDir, QStringLiteral("Jegyzetek mappája")));
    dirsForm->addRow(QStringLiteral("Metaadat mappája:"),
                     makeDirRow(dirsBox, m_metadataDir, QStringLiteral("Metaadat mappája")));
    gl->addWidget(dirsBox);
    gl->addStretch(1);
    tabs->addTab(generalPage, QStringLiteral("Általános"));

    // ============================ Fül 2: Rögzítés =============================
    // Minden a hangfelvételről: auto-rögzítés + mely eszközöket (sávokat) vegyük fel.
    auto* recPage = new QWidget(this);
    auto* rl = new QVBoxLayout(recPage);
    m_autoRecord = new QCheckBox(
        QStringLiteral("Automatikus rögzítés (minden eszköz)"), recPage);
    m_autoRecord->setToolTip(QStringLiteral(
        "Bekapcsolva minden bemenetet rögzít; a csendes sávokat a felvétel után "
        "automatikusan eldobja (a fájl megmarad, visszaállítható). Kikapcsolva az "
        "alább kijelölt eszközöket rögzíti."));
    rl->addWidget(m_autoRecord);

    m_devicesGroup = new QGroupBox(QStringLiteral("Rögzítendő eszközök (alapértelmezés)"), recPage);
    auto* dvl = new QVBoxLayout(m_devicesGroup);
    auto* devHint = new QLabel(QStringLiteral(
        "Mely eszközöket (sávokat) vegye fel alapból kézi módban. A vonalbemenet/AUX "
        "alapból kimarad (kézzel bepipálható). Auto-rögzítésnél ez a választás nem számít."),
        m_devicesGroup);
    devHint->setWordWrap(true);
    applyMuted(devHint);
    dvl->addWidget(devHint);
    buildDevicePolicy(dvl);
    rl->addWidget(m_devicesGroup);

    // --- Hangminőség + lekeverés ---
    auto* qualBox  = new QGroupBox(QStringLiteral("Hangminőség és lekeverés"), recPage);
    auto* qualForm = new QFormLayout(qualBox);
    qualForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

    // Per-sáv Opus bitráta. A legalsó fokozat is „STT-biztos" (24 kbps); lejjebb nem megyünk,
    // hogy a per-sáv .ogg-ból dolgozó átírás pontossága ne romoljon.
    m_audioQuality = new QComboBox(qualBox);
    m_audioQuality->addItem(QStringLiteral("Legjobb (64 kbps)"),  QStringLiteral("best"));
    m_audioQuality->addItem(QStringLiteral("Magas (48 kbps)"),    QStringLiteral("high"));
    m_audioQuality->addItem(QStringLiteral("Közepes (32 kbps)"),  QStringLiteral("medium"));
    m_audioQuality->addItem(QStringLiteral("Takarékos (24 kbps)"),QStringLiteral("low"));
    qualForm->addRow(QStringLiteral("Hangminőség:"), m_audioQuality);

    m_qualityHint = new QLabel(qualBox);
    m_qualityHint->setWordWrap(true);
    applyMuted(m_qualityHint);
    qualForm->addRow(QString(), m_qualityHint);

    auto sizeHintFor = [](const QString& id) -> QString {
        // ~1,5 órás felvétel egy sávra; egy meetingben jellemzően 2-4 sáv.
        if (id == QStringLiteral("low"))    return QStringLiteral("~16 MB / sáv / 1,5h — a legkisebb, STT-talp.");
        if (id == QStringLiteral("medium")) return QStringLiteral("~22 MB / sáv / 1,5h — jó beszédre.");
        if (id == QStringLiteral("high"))   return QStringLiteral("~32 MB / sáv / 1,5h.");
        return QStringLiteral("~43 MB / sáv / 1,5h — a legjobb (jelenlegi alap).");
    };
    connect(m_audioQuality, &QComboBox::currentIndexChanged, this,
            [this, sizeHintFor]() {
                m_qualityHint->setText(sizeHintFor(m_audioQuality->currentData().toString()));
            });
    // Kezdő hint az aktuális (index 0 = Legjobb) fokozatra; a loadGeneral() utána a mentett
    // szintre állítja az indexet — ha az nem 0, a currentIndexChanged frissíti a hintet.
    m_qualityHint->setText(sizeHintFor(m_audioQuality->currentData().toString()));

    // Lekeverés időzítése. A mixdown CSAK hallgatásra kell (az átírás a per-sáv .ogg-kból
    // megy), ezért a leállítás sosem várja meg → nincs UI-fagyás.
    m_mixdownMode = new QComboBox(qualBox);
    m_mixdownMode->addItem(QStringLiteral("Automatikusan, a felvétel után (háttérben)"),
                           QStringLiteral("auto"));
    m_mixdownMode->addItem(QStringLiteral("Kézzel, később (a felvétel paneljéből)"),
                           QStringLiteral("manual"));
    m_mixdownMode->setToolTip(QStringLiteral(
        "A lekevert, normalizált fájl csak kényelmes hallgatásra kell — az átíráshoz nem. "
        "Automatikus módban a felvétel után a háttérben készül el (nem fagyaszt, közben új "
        "felvétel is indítható). Kézi módban a felvétel review-paneljén indíthatod."));
    qualForm->addRow(QStringLiteral("Lekeverés:"), m_mixdownMode);

    rl->addWidget(qualBox);
    rl->addStretch(1);

    // Auto-rögzítéskor a per-eszköz választás moot → letiltjuk a listát.
    connect(m_autoRecord, &QCheckBox::toggled, this, [this](bool on) {
        if (m_devicesGroup) m_devicesGroup->setEnabled(!on);
    });
    tabs->addTab(recPage, QStringLiteral("Rögzítés"));

    // ============================ Fül: Figyelő ===============================
    // Háttér-detektor: érzékeli, ha aktív hívásban vagy (egy hívás-app fogja a
    // mikrofont), és a tálcáról felajánlja a rögzítést. A figyelő külön, könnyű
    // folyamat; ezek a beállítások közösek (~/.tanara/settings.json).
    auto* watchPage = new QWidget(this);
    auto* wl = new QVBoxLayout(watchPage);

    m_detectorEnabled = new QCheckBox(
        QStringLiteral("Aktív hívás észlelése (a tálca-figyelő felajánlja a rögzítést)"), watchPage);
    m_detectorEnabled->setToolTip(QStringLiteral(
        "Bekapcsolva a háttér-figyelő időnként megnézi, fogja-e egy ismert hívás-app a "
        "mikrofont, és értesítéssel felajánlja a felvétel indítását. Csak figyel — a "
        "rögzítéshez a felvevőt indítja."));
    wl->addWidget(m_detectorEnabled);

    auto* watchForm = new QFormLayout();
    m_detectorInterval = new QSpinBox(watchPage);
    m_detectorInterval->setRange(3, 60);
    m_detectorInterval->setSuffix(QStringLiteral(" mp"));
    m_detectorInterval->setToolTip(QStringLiteral(
        "Milyen gyakran nézzen körül a figyelő. Rövidebb = gyorsabb felajánlás, több CPU."));
    watchForm->addRow(QStringLiteral("Ellenőrzés gyakorisága:"), m_detectorInterval);

    m_watcherAutostart = new QCheckBox(
        QStringLiteral("A figyelő induljon bejelentkezéskor"), watchPage);
    m_watcherAutostart->setToolTip(QStringLiteral(
        "Bejelentkezéskor automatikusan elindul a háttér-figyelő (a rendszertálcára dokkolva)."));
    watchForm->addRow(QString(), m_watcherAutostart);
    wl->addLayout(watchForm);

    auto* appsBox = new QGroupBox(QStringLiteral("Ismert hívás-appok"), watchPage);
    auto* abl = new QVBoxLayout(appsBox);
    auto* appsHint = new QLabel(QStringLiteral(
        "Soronként egy app (bináris- vagy név-részlet, pl. „zoom”, „teams”). A figyelő "
        "ezekre jelez, ha aktívan fogják a mikrofont."), appsBox);
    appsHint->setWordWrap(true);
    applyMuted(appsHint);
    abl->addWidget(appsHint);
    m_knownCallApps = new QPlainTextEdit(appsBox);
    m_knownCallApps->setPlaceholderText(QStringLiteral("zoom\nteams\nmeet\ndiscord…"));
    m_knownCallApps->setFixedHeight(120);
    abl->addWidget(m_knownCallApps);
    wl->addWidget(appsBox);
    wl->addStretch(1);

    // Kikapcsolt észlelésnél a többi mező moot → letiltjuk.
    connect(m_detectorEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (m_detectorInterval) m_detectorInterval->setEnabled(on);
        if (m_watcherAutostart) m_watcherAutostart->setEnabled(on);
        if (m_knownCallApps)    m_knownCallApps->setEnabled(on);
    });
    tabs->addTab(watchPage, QStringLiteral("Figyelő"));

    // ===================== Fül 3: Külső szolgáltatások ========================
    // Az átírás + összefoglaló NEM a Tanarában fut — külső szolgáltatás a saját
    // kulcsoddal / végpontoddal. Lock-in nincs: bármikor válthatsz.
    auto* extPage = new QWidget(this);
    auto* el = new QVBoxLayout(extPage);
    auto* extIntro = new QLabel(QStringLiteral(
        "Az átírást és az összefoglalót KÜLSŐ szolgáltatások végzik a saját kulcsoddal / "
        "végpontoddal — ezeket nem a Tanara futtatja. Bármikor válthatsz, nincs lock-in."),
        extPage);
    extIntro->setWordWrap(true);
    applyMuted(extIntro);
    el->addWidget(extIntro);

    // Segéd: egy provider-blokk (combo + üres mező-form) felépítése.
    auto buildProviderBox = [this](ProviderSection& section, ProviderKind kind,
                                   const QString& title,
                                   const QVector<ProviderDescriptor>& providers,
                                   const QString& selectedId) {
        section.kind = kind;
        auto* box = new QGroupBox(title, this);
        auto* outer = new QVBoxLayout(box);

        auto* selRow = new QFormLayout();
        section.selector = new QComboBox(box);
        for (const ProviderDescriptor& d : providers)
            section.selector->addItem(d.displayName, d.id);
        int idx = section.selector->findData(selectedId);
        if (idx < 0 && section.selector->count() > 0)
            idx = 0;
        if (idx >= 0)
            section.selector->setCurrentIndex(idx);
        selRow->addRow(QStringLiteral("Szolgáltató:"), section.selector);
        outer->addLayout(selRow);

        section.fieldsForm = new QFormLayout();
        outer->addLayout(section.fieldsForm);

        connect(section.selector, &QComboBox::currentIndexChanged, this,
                [this, &section]() {
                    // Váltás előtt a folyamatban lévő (még be nem írt) mezőket a
                    // RÉGI provider configjába mentjük, hogy ne vesszenek el.
                    collectSection(section);
                    rebuildFields(section, section.selector->currentData().toString());
                });

        const QString initialId =
            section.selector->currentIndex() >= 0
                ? section.selector->currentData().toString()
                : QString();
        rebuildFields(section, initialId);
        return box;
    };

    el->addWidget(buildProviderBox(
        m_stt, ProviderKind::Stt, QStringLiteral("Átírás (STT)"),
        tanara::SttProviderRegistry::instance().all(), s.sttProviderId));
    el->addWidget(buildProviderBox(
        m_llm, ProviderKind::Llm, QStringLiteral("Összefoglaló (LLM)"),
        tanara::LlmProviderRegistry::instance().all(), s.llmProviderId));
    el->addStretch(1);
    tabs->addTab(extPage, QStringLiteral("Külső szolgáltatások"));

    // ======================== Fül 4: Összefoglaló ============================
    // Az összefoglaló LLM rendszer-promptja — szabadon hangolható (séma + szabályok).
    // Üresen hagyva / alapértelmezettel megegyezve a beépített default érvényes.
    auto* sumPage = new QWidget(this);
    auto* sumL = new QVBoxLayout(sumPage);
    auto* sumIntro = new QLabel(QStringLiteral(
        "Az összefoglalót készítő modell rendszer-promptjai (utasítások + JSON-séma). A "
        "választóval válthatsz az egyszerű, egy-körös prompt és a komplex (több körös) mód két "
        "prompt-ja között. A kapott átirat és a kontextus automatikusan a prompt UTÁN kerül a modellhez."),
        sumPage);
    sumIntro->setWordWrap(true);
    applyMuted(sumIntro);
    sumL->addWidget(sumIntro);

    auto* selRow = new QHBoxLayout();
    selRow->addWidget(new QLabel(QStringLiteral("Prompt:"), sumPage), 0);
    m_promptSelect = new QComboBox(sumPage);
    m_promptSelect->addItem(QStringLiteral("Egyszerű összefoglaló"),  QStringLiteral("simple"));
    m_promptSelect->addItem(QStringLiteral("Komplex — Téma-kinyerés (1. kör)"), QStringLiteral("topic"));
    m_promptSelect->addItem(QStringLiteral("Komplex — Téma-elemzés (2. kör)"),  QStringLiteral("analysis"));
    selRow->addWidget(m_promptSelect, 1);
    sumL->addLayout(selRow);

    m_summaryPrompt = new QPlainTextEdit(sumPage);
    m_summaryPrompt->setPlaceholderText(QStringLiteral("Rendszer-prompt…"));
    sumL->addWidget(m_summaryPrompt, 1);

    auto* resetRow = new QHBoxLayout();
    resetRow->addStretch(1);
    auto* resetBtn = new QPushButton(QStringLiteral("Visszaállítás alapértelmezettre"), sumPage);
    resetRow->addWidget(resetBtn);
    sumL->addLayout(resetRow);
    // Váltáskor a jelenlegi szerkesztő-tartalmat elmentjük a régi promptba, és betöltjük az újat.
    connect(m_promptSelect, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_curPromptId.isEmpty())
            m_promptText[m_curPromptId] = m_summaryPrompt->toPlainText();
        m_curPromptId = m_promptSelect->currentData().toString();
        m_summaryPrompt->setPlainText(m_promptText.value(m_curPromptId));
    });
    // Reset csak a KIVÁLASZTOTT prompt defaultjára.
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_summaryPrompt->setPlainText(summaryPromptDefault(m_curPromptId));
    });
    tabs->addTab(sumPage, QStringLiteral("Összefoglaló"));

    root->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // LLM-modellek lekérése a meglévő AppController-úton (megőrzött működés).
    if (m_controller) {
        connect(m_controller, &tanara::AppController::llmModelsFetched,
                this, &SettingsDialog::onLlmModelsFetched);
        connect(m_controller, &tanara::AppController::llmModelsFailed,
                this, &SettingsDialog::onLlmModelsFailed);
    }

    loadGeneral();
}

void SettingsDialog::wireFolderPicker(QLineEdit* field, QPushButton* button,
                                      const QString& caption) {
    connect(button, &QPushButton::clicked, this, [this, field, caption]() {
        QString start = field->text().trimmed();
        if (start.isEmpty())
            start = QDir::homePath();
        const QString dir = QFileDialog::getExistingDirectory(this, caption, start);
        if (!dir.isEmpty())
            field->setText(dir);
    });
}

void SettingsDialog::buildDevicePolicy(QVBoxLayout* into) {
    m_deviceChecks.clear();
    const QStringList lastUsed =
        m_controller ? m_controller->lastUsedDeviceNames() : QStringList{};
    const QVector<tanara::AudioDeviceInfo> devs =
        (m_controller && m_controller->devices())
            ? m_controller->devices()->captureDevices()
            : QVector<tanara::AudioDeviceInfo>{};

    if (devs.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("Nincs észlelt hangeszköz."), m_devicesGroup);
        none->setForegroundRole(QPalette::PlaceholderText);
        into->addWidget(none);
        return;
    }

    struct KindRow { tanara::TrackKind kind; QString title; };
    const KindRow order[] = {
        { tanara::TrackKind::Mic,      QStringLiteral("Mikrofonok") },
        { tanara::TrackKind::Loopback, QStringLiteral("Rendszerhang (loopback)") },
        { tanara::TrackKind::Other,    QStringLiteral("Egyéb (vonalbemenet/AUX)") },
    };
    for (const KindRow& kr : order) {
        QVector<tanara::AudioDeviceInfo> group;
        for (const auto& d : devs)
            if (d.kind == kr.kind) group.push_back(d);
        if (group.isEmpty())
            continue;
        auto* header = new QLabel(kr.title, m_devicesGroup);
        // Bold a QFont-on (NEM stylesheet) — a stylesheet szín nélkül fekete alapszínt
        // erőltetne, ami sötét témán olvashatatlan.
        QFont hf = header->font();
        hf.setBold(true);
        header->setFont(hf);
        into->addWidget(header);
        for (const auto& d : group) {
            auto* cb = new QCheckBox(
                d.name + (d.isDefault ? QStringLiteral("  (alapértelmezett)") : QString()),
                m_devicesGroup);
            // Előpipálás: a perzisztens default-halmaz (lastUsed); ha üres, a rendszer-
            // alapértelmezett (de a line-in/AUX alapból kimarad).
            const bool checked = lastUsed.isEmpty()
                ? (d.isDefault && d.kind != tanara::TrackKind::Other)
                : lastUsed.contains(d.name);
            cb->setChecked(checked);
            into->addWidget(cb);
            m_deviceChecks.insert(d.name, cb);   // azonos név → utolsó nyer (név-alapú a választás)
        }
    }
}

QWidget* SettingsDialog::makeWidgetFor(ProviderSection& section, const ConfigField& field,
                                       QWidget* parent) {
    switch (field.type) {
    case ConfigFieldType::Secret: {
        auto* le = new QLineEdit(parent);
        le->setEchoMode(QLineEdit::Password);
        le->setPlaceholderText(QStringLiteral("(változatlan, ha üresen hagyod)"));
        return le;
    }
    case ConfigFieldType::Number: {
        if (field.key == QLatin1String("temperature")) {
            auto* sb = new QDoubleSpinBox(parent);
            sb->setRange(0.0, 2.0);
            sb->setSingleStep(0.1);
            sb->setDecimals(2);
            return sb;
        }
        auto* sb = new QSpinBox(parent);
        sb->setRange(0, 1000000);
        sb->setSingleStep(512);
        return sb;
    }
    case ConfigFieldType::Combo: {
        // dynamicOptions → szerkeszthető combo + "Modellek lekérése" gomb egy sorban.
        if (field.dynamicOptions) {
            auto* container = new QWidget(parent);
            auto* h = new QHBoxLayout(container);
            h->setContentsMargins(0, 0, 0, 0);
            auto* combo = new QComboBox(container);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            for (const ConfigOption& o : field.options)
                combo->addItem(o.label.isEmpty() ? o.value : o.label, o.value);
            auto* fetchBtn = new QPushButton(QStringLiteral("Modellek lekérése"), container);
            h->addWidget(combo, 1);
            h->addWidget(fetchBtn, 0);

            section.dynamicCombo = combo;
            if (m_controller) {
                connect(fetchBtn, &QPushButton::clicked, this, [this, fetchBtn, &section]() {
                    if (section.statusLabel)
                        section.statusLabel->setVisible(false);
                    fetchBtn->setEnabled(false);
                    fetchBtn->setText(QStringLiteral("Lekérés…"));
                    m_controller->fetchLlmModels();
                });
            } else {
                fetchBtn->setEnabled(false);
            }
            // A combo a "logikai" widget (innen olvassuk/írjuk az értéket).
            container->setProperty("tanaraEditor",
                                   QVariant::fromValue<QObject*>(combo));
            return container;
        }
        auto* combo = new QComboBox(parent);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        for (const ConfigOption& o : field.options)
            combo->addItem(o.label.isEmpty() ? o.value : o.label, o.value);
        return combo;
    }
    case ConfigFieldType::Url:
    case ConfigFieldType::Text:
    default: {
        auto* le = new QLineEdit(parent);
        return le;
    }
    }
}

void SettingsDialog::rebuildFields(ProviderSection& section, const QString& providerId) {
    // Régi mezők kiürítése.
    while (section.fieldsForm->rowCount() > 0)
        section.fieldsForm->removeRow(0);
    section.widgets.clear();
    section.dynamicCombo = nullptr;
    section.statusLabel = nullptr;
    section.currentId = providerId;

    if (providerId.isEmpty())
        return;

    const ProviderDescriptor desc =
        section.kind == ProviderKind::Stt
            ? tanara::SttProviderRegistry::instance().descriptor(providerId)
            : tanara::LlmProviderRegistry::instance().descriptor(providerId);

    // A kiválasztott provider aktuális (mentett) configja a load-hoz.
    ProviderConfig cfg;
    if (m_controller && m_controller->settings()) {
        const tanara::AppSettings s = m_controller->settings()->settings();
        const auto& map = section.kind == ProviderKind::Stt ? s.sttConfigs : s.llmConfigs;
        cfg = map.value(providerId);
    }

    QWidget* box = section.fieldsForm->parentWidget();
    for (const ConfigField& field : desc.fields) {
        QWidget* w = makeWidgetFor(section, field, box);

        // A "logikai" szerkesztő widget (dynamic combo esetén a konténerből).
        QWidget* editor = w;
        if (auto* inner = qobject_cast<QWidget*>(
                w->property("tanaraEditor").value<QObject*>()))
            editor = inner;

        // Súgó: tooltip + halvány, de OLVASHATÓ súgó-sor.
        if (!field.help.isEmpty()) {
            editor->setToolTip(field.help);
            w->setToolTip(field.help);
        }

        // Load: titkot SOHA nem töltünk vissza. A jól-ismert kulcsok a
        // ProviderConfig mezőiből, az egyebek az extra-ból; ha nincs érték,
        // a descriptor defaultValue-ja.
        if (!field.isSecret) {
            QString value;
            bool hasStored;
            if (isWellKnownKey(field.key)) {
                value = wellKnownValue(cfg, field.key);
                hasStored = true;  // a jól-ismert kulcs mindig ad string-et
            } else {
                hasStored = cfg.extra.contains(field.key);
                if (hasStored)
                    value = cfg.extra.value(field.key).toString();
            }
            // Per-mező default fallback: ha a tárolt érték "üres" (Number-nél a 0 is
            // üresnek számít, így a migrált maxTokens=0 a descriptor-defaultot kapja),
            // a descriptor defaultValue-jával töltjük. Így egy részlegesen kitöltött
            // config sem mutat 0-t a 8000 helyett.
            const bool emptyForField =
                !hasStored
                || value.isEmpty()
                || (field.type == ConfigFieldType::Number && value.toDouble() == 0.0);
            if (emptyForField && !field.defaultValue.isEmpty())
                value = field.defaultValue;

            if (auto* le = qobject_cast<QLineEdit*>(editor)) {
                le->setText(value);
            } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(editor)) {
                dsb->setValue(value.toDouble());
            } else if (auto* sb = qobject_cast<QSpinBox*>(editor)) {
                sb->setValue(value.toInt());
            } else if (auto* cb = qobject_cast<QComboBox*>(editor)) {
                if (!value.isEmpty())
                    cb->setEditText(value);
            }
        }

        QString label = field.label.isEmpty() ? field.key : field.label;
        if (field.required)
            label += QStringLiteral(" *");
        section.fieldsForm->addRow(label + QStringLiteral(":"), w);

        section.widgets.insert(field.key, editor);
    }

    // A dinamikus modell-lekérés státusz-sora (olvasható hibaszín).
    if (section.dynamicCombo) {
        section.statusLabel = new QLabel(box);
        section.statusLabel->setWordWrap(true);
        section.statusLabel->setStyleSheet(QStringLiteral("color: #d33;"));
        section.statusLabel->setVisible(false);
        section.fieldsForm->addRow(QString(), section.statusLabel);
    }
}

void SettingsDialog::onLlmModelsFetched(const QStringList& models) {
    QComboBox* combo = m_llm.dynamicCombo;
    if (!combo)
        return;
    if (m_llm.statusLabel)
        m_llm.statusLabel->setVisible(false);

    const QString current = combo->currentText();
    combo->clear();
    combo->addItems(models);
    const int idx = combo->findText(current);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    else
        combo->setEditText(current);  // kézi értéket megtartjuk, ha nincs a listában
}

void SettingsDialog::onLlmModelsFailed(const QString& error) {
    if (m_llm.statusLabel) {
        m_llm.statusLabel->setText(
            QStringLiteral("Nem sikerült lekérni a modelleket: %1").arg(error));
        m_llm.statusLabel->setVisible(true);
    }
}

void SettingsDialog::loadGeneral() {
    if (!m_controller || !m_controller->settings())
        return;
    const tanara::AppSettings s = m_controller->settings()->settings();
    m_audioDir->setText(s.audioDir);
    m_notesDir->setText(s.notesDir);
    m_metadataDir->setText(s.metadataDir);
    m_userSpeakerName->setText(s.userSpeakerName);
    m_autoRecord->setChecked(s.autoRecordAllDevices);
    if (m_audioQuality) {
        int qi = m_audioQuality->findData(s.audioQuality);
        m_audioQuality->setCurrentIndex(qi >= 0 ? qi : 0);   // a hintet a signal frissíti, ha változik
    }
    if (m_mixdownMode) {
        int mi = m_mixdownMode->findData(s.mixdownMode);
        m_mixdownMode->setCurrentIndex(mi >= 0 ? mi : 0);
    }
    if (m_summaryPrompt && m_promptSelect) {
        // A három prompt-puffer feltöltése (üres beállítás → a beépített default).
        auto initBuf = [&](const QString& id, const QString& stored) {
            m_promptText[id] = stored.isEmpty() ? summaryPromptDefault(id) : stored;
        };
        initBuf(QStringLiteral("simple"),   s.summaryPrompt);
        initBuf(QStringLiteral("topic"),    s.topicExtractionPrompt);
        initBuf(QStringLiteral("analysis"), s.topicAnalysisPrompt);
        m_promptSelect->setCurrentIndex(0);
        m_curPromptId = m_promptSelect->currentData().toString();   // "simple"
        m_summaryPrompt->setPlainText(m_promptText.value(m_curPromptId));
    }

    // Figyelő fül.
    if (m_detectorEnabled) {
        m_detectorEnabled->setChecked(s.detectorEnabled);
        if (m_detectorInterval)  m_detectorInterval->setValue(s.detectorIntervalSec);
        if (m_watcherAutostart)  m_watcherAutostart->setChecked(s.watcherAutostart);
        if (m_knownCallApps)     m_knownCallApps->setPlainText(s.knownCallApps.join(QLatin1Char('\n')));
        // a többi mező engedélyezése az észlelés-kapcsoló szerint
        if (m_detectorInterval)  m_detectorInterval->setEnabled(s.detectorEnabled);
        if (m_watcherAutostart)  m_watcherAutostart->setEnabled(s.detectorEnabled);
        if (m_knownCallApps)     m_knownCallApps->setEnabled(s.detectorEnabled);
    }
    // A provider-mezőket a rebuildFields() tölti (ctorban + váltáskor).
}

void SettingsDialog::collectSection(ProviderSection& section) {
    if (section.currentId.isEmpty() || !m_controller || !m_controller->settings())
        return;

    const ProviderDescriptor desc =
        section.kind == ProviderKind::Stt
            ? tanara::SttProviderRegistry::instance().descriptor(section.currentId)
            : tanara::LlmProviderRegistry::instance().descriptor(section.currentId);

    tanara::AppSettings s = m_controller->settings()->settings();
    auto& map = section.kind == ProviderKind::Stt ? s.sttConfigs : s.llmConfigs;

    ProviderConfig cfg = map.value(section.currentId);
    cfg.type = section.currentId;

    for (const ConfigField& field : desc.fields) {
        QWidget* editor = section.widgets.value(field.key);
        if (!editor)
            continue;

        // A widget aktuális string-értékének kiolvasása.
        QString value;
        double dvalue = 0.0;
        int ivalue = 0;
        if (auto* le = qobject_cast<QLineEdit*>(editor)) {
            value = le->text().trimmed();
        } else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(editor)) {
            dvalue = dsb->value();
            value = QString::number(dvalue);
        } else if (auto* sb = qobject_cast<QSpinBox*>(editor)) {
            ivalue = sb->value();
            value = QString::number(ivalue);
        } else if (auto* cb = qobject_cast<QComboBox*>(editor)) {
            value = cb->currentText().trimmed();
        }

        // Titok → KeyStore (nem perzisztálódik a configba); üres = változatlan.
        if (field.isSecret) {
            if (!value.isEmpty() && !field.secretKey.isEmpty())
                m_controller->setSecret(field.secretKey, value);
            continue;
        }

        // Jól-ismert kulcsok → ProviderConfig mező; egyéb → extra.
        if (field.key == QLatin1String("baseUrl")) {
            cfg.baseUrl = value;
        } else if (field.key == QLatin1String("model")) {
            cfg.model = value;
        } else if (field.key == QLatin1String("temperature")) {
            cfg.temperature = dvalue;
        } else if (field.key == QLatin1String("maxTokens")) {
            cfg.maxTokens = ivalue;
        } else {
            cfg.extra.insert(field.key, value);
        }
    }

    map.insert(section.currentId, cfg);
    if (section.kind == ProviderKind::Stt)
        s.sttProviderId = section.currentId;
    else
        s.llmProviderId = section.currentId;

    m_controller->settings()->setSettings(s);
}

void SettingsDialog::onAccept() {
    if (!m_controller || !m_controller->settings()) {
        accept();
        return;
    }

    tanara::AppSettings s = m_controller->settings()->settings();
    s.audioDir = m_audioDir->text().trimmed();
    s.notesDir = m_notesDir->text().trimmed();
    s.metadataDir = m_metadataDir->text().trimmed();
    s.userSpeakerName = m_userSpeakerName->text().trimmed();
    s.autoRecordAllDevices = m_autoRecord->isChecked();
    if (m_audioQuality && m_audioQuality->currentIndex() >= 0)
        s.audioQuality = m_audioQuality->currentData().toString();
    if (m_mixdownMode && m_mixdownMode->currentIndex() >= 0)
        s.mixdownMode = m_mixdownMode->currentData().toString();
    if (m_summaryPrompt && m_promptSelect) {
        // A jelenleg szerkesztett prompt szövegét a pufferbe szinkronizáljuk, majd mindhárom
        // promptot mentjük: ha a szöveg == a beépített default, ÜRESEN (a kód-default jövőbeli
        // javításai így érvényesülnek), különben a saját szöveget.
        if (!m_curPromptId.isEmpty())
            m_promptText[m_curPromptId] = m_summaryPrompt->toPlainText();
        auto store = [&](const QString& id) -> QString {
            const QString p = m_promptText.value(id);
            return p.trimmed() == summaryPromptDefault(id).trimmed() ? QString() : p;
        };
        s.summaryPrompt          = store(QStringLiteral("simple"));
        s.topicExtractionPrompt  = store(QStringLiteral("topic"));
        s.topicAnalysisPrompt    = store(QStringLiteral("analysis"));
    }

    // Figyelő fül.
    if (m_detectorEnabled) {
        s.detectorEnabled = m_detectorEnabled->isChecked();
        if (m_detectorInterval)  s.detectorIntervalSec = m_detectorInterval->value();
        if (m_watcherAutostart)  s.watcherAutostart = m_watcherAutostart->isChecked();
        if (m_knownCallApps) {
            QStringList apps;
            const QStringList lines = m_knownCallApps->toPlainText().split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                const QString a = line.trimmed();
                if (!a.isEmpty()) apps << a;
            }
            if (!apps.isEmpty())   // üres lista → megtartjuk a defaultot
                s.knownCallApps = apps;
        }
    }
    m_controller->settings()->setSettings(s);

    // Eszköz-policy mentése (a felvevővel közös default-halmaz). Csak ha volt mit
    // megjeleníteni (különben nem nulláznánk feleslegesen a lastUsed-et).
    if (!m_deviceChecks.isEmpty()) {
        QStringList chosen;
        for (auto it = m_deviceChecks.constBegin(); it != m_deviceChecks.constEnd(); ++it)
            if (it.value() && it.value()->isChecked())
                chosen << it.key();
        m_controller->setLastUsedDeviceNames(chosen);
    }

    // A provider-szekciók a saját configjukat + titkaikat a friss settings-be írják.
    collectSection(m_stt);
    collectSection(m_llm);

    accept();
}

} // namespace tanara_gui
