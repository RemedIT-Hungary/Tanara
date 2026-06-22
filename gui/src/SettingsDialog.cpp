#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/Types.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/audio/DeviceManager.h"

#include <QLineEdit>
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
    rl->addStretch(1);

    // Auto-rögzítéskor a per-eszköz választás moot → letiltjuk a listát.
    connect(m_autoRecord, &QCheckBox::toggled, this, [this](bool on) {
        if (m_devicesGroup) m_devicesGroup->setEnabled(!on);
    });
    tabs->addTab(recPage, QStringLiteral("Rögzítés"));

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
