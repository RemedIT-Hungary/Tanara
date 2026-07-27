#include "RecordBar.h"
#include "ui_RecordBar.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QDateTime>
#include <QEvent>
#include <QVariant>
#include <algorithm>

namespace tanara_gui {

namespace {
// "Vizuális erősítés": a halk beszéd is láthatóan mozgassa a sávot.
constexpr float kLevelVisualGain = 140.0f;

QString groupTitle(tanara::TrackKind kind) {
    switch (kind) {
    case tanara::TrackKind::Mic:      return QCoreApplication::translate("RecordBar", "🎤 Mikrofonok");
    case tanara::TrackKind::Loopback: return QCoreApplication::translate("RecordBar", "🔊 Hangkimenetek");
    default:                          return QCoreApplication::translate("RecordBar", "Egyéb");
    }
}
} // namespace

RecordBar::RecordBar(tanara::AppController* controller, QWidget* parent)
    : QWidget(parent), ui(new Ui::RecordBar), m_controller(controller) {

    // A STATIKUS vázat a RecordBar.ui adja (Designerből szerkeszthető); a
    // tag-pointereket innen kötjük be, a viselkedés/paraméterek alább, kódban maradnak.
    ui->setupUi(this);
    m_titleLabel   = ui->titleLabel;
    m_editBtn      = ui->editBtn;
    m_titleEdit    = ui->titleEdit;
    m_recordBtn    = ui->recordBtn;
    m_minBtn       = ui->minBtn;
    m_closeBtn     = ui->closeBtn;
    m_tracksToggle = ui->tracksToggle;
    m_voicesLabel  = ui->voicesLabel;
    m_levelsBox    = ui->levelsBox;
    m_devHint      = ui->devHint;
    m_deviceList   = ui->deviceList;

    // --- viselkedés / paraméterek (kódban) ---
    // Alap cím dátum NÉLKÜL (az időpont a meeting metaadatában úgyis ott van).
    m_titleEdit->setText(tr("Megbeszélés"));
    updateTitleDisplay();
    // A cím alapból label; dupla kattintásra / ✏-re vált szerkesztőre.
    if (m_titleLabel)
        m_titleLabel->installEventFilter(this);
    if (m_editBtn)
        connect(m_editBtn, &QToolButton::clicked, this, &RecordBar::enterTitleEdit);
    if (m_titleEdit)
        connect(m_titleEdit, &QLineEdit::editingFinished, this, &RecordBar::commitTitleEdit);
    m_recordBtn->setMinimumWidth(150);
    m_deviceList->setSelectionMode(QAbstractItemView::NoSelection);

    // A hangforrás-doboz alapból rejtett; a "▸ Rögzítendő hangforrások módosítása" toggle mutatja.
    m_levelsVisible = false;
    if (m_levelsBox) m_levelsBox->setVisible(false);

    connect(m_recordBtn, &QPushButton::clicked, this, &RecordBar::onStartStopClicked);
    // Frameless ablak-vezérlők (alapból rejtve; a FloatingRecorder kapcsolja be).
    if (m_minBtn)
        connect(m_minBtn, &QToolButton::clicked, this, &RecordBar::minimizeRequested);
    if (m_closeBtn)
        connect(m_closeBtn, &QToolButton::clicked, this, &RecordBar::closeRequested);
    // "▸ Rögzítendő hangforrások módosítása" — a hangforrás-dobozt nyitja/zárja
    // (itt pipálhatók a források ÉS itt mozog a VU-sávjuk).
    connect(m_tracksToggle, &QToolButton::clicked, this, [this]() {
        setLevelsVisible(!m_levelsVisible);
    });

    rebuildDeviceList();
    onRecordingStateChanged(m_controller ? m_controller->recordingState()
                                         : tanara::RecordingState::Idle);
    applyViewMode();
}

RecordBar::~RecordBar() { delete ui; }

void RecordBar::setWindowControlsVisible(bool on) {
    if (m_minBtn)   m_minBtn->setVisible(on);
    if (m_closeBtn) m_closeBtn->setVisible(on);
}

void RecordBar::updateTitleDisplay() {
    if (!m_titleLabel || !m_titleEdit)
        return;
    const QString t = m_titleEdit->text().trimmed();
    m_titleLabel->setText(t.isEmpty() ? tr("Megbeszélés") : t);
}

void RecordBar::enterTitleEdit() {
    if (!m_titleEdit || m_state != tanara::RecordingState::Idle)
        return;   // rögzítés közben a cím nem szerkeszthető
    if (m_titleLabel) m_titleLabel->hide();
    if (m_editBtn)    m_editBtn->hide();
    m_titleEdit->show();
    m_titleEdit->setFocus();
    m_titleEdit->selectAll();
}

void RecordBar::commitTitleEdit() {
    if (!m_titleEdit)
        return;
    m_titleEdit->hide();
    updateTitleDisplay();
    if (m_titleLabel) m_titleLabel->show();
    if (m_editBtn && m_state == tanara::RecordingState::Idle)
        m_editBtn->show();
}

bool RecordBar::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_titleLabel && ev->type() == QEvent::MouseButtonDblClick) {
        enterTitleEdit();
        return true;   // a dupla katt a szerkesztést indítja (nem propagál tovább)
    }
    return QWidget::eventFilter(obj, ev);
}

void RecordBar::refreshFromSettings() {
    if (!m_controller || !m_controller->devices())
        return;
    const bool autoAll = m_controller->settings()
                         && m_controller->settings()->settings().autoRecordAllDevices;
    // Mely forrásoknak kell bepipálva lenniük a Beállítások szerint:
    //   auto-mód → minden auto-rögzítendő (line-in/AUX kihagyva),
    //   kézi mód → a mentett kiválasztás (lastUsedDeviceNames).
    QStringList wanted;
    if (autoAll) {
        for (const auto& d : m_controller->devices()->autoRecordDevices())
            wanted << d.name;
    } else {
        wanted = m_controller->lastUsedDeviceNames();
    }
    for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it) {
        if (!it.value().check)
            continue;
        // A programozott setChecked NE mentsen vissza (különben felülírná a policyt).
        QSignalBlocker block(it.value().check);
        it.value().check->setChecked(wanted.contains(it.key()));
    }
    updateVoicesLabel();
}

void RecordBar::setViewMode(ViewMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    applyViewMode();
    emit viewModeChanged(mode);
}

void RecordBar::applyViewMode() {
    const bool compact = (m_mode == ViewMode::Compact);
    // A cím MINDIG label-ként indul; a titleEdit-et csak a ✏/dupla-katt nyitja meg
    // (lásd enterTitleEdit/commitTitleEdit) — a nézettől FÜGGETLENÜL. Korábban itt egy
    // maradék `m_titleEdit->setVisible(!compact)` Full nézetben felhozta az input mezőt a
    // label MELLÉ (mindkettő látszott induláskor) → ezt kivettük; a titleEdit láthatóságát
    // már csak a szerkesztés be/kilépése vezérli.
    if (m_devHint)   m_devHint->setVisible(!compact);

    // Csoportfejek: kompaktban rejtve.
    for (QListWidgetItem* h : m_headerItems)
        if (h) h->setHidden(compact);
    // Eszköz-sorok: kompaktban csak a bepipáltak, checkbox nélkül.
    for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it) {
        DeviceRow& r = it.value();
        const bool checked = r.check && r.check->isChecked();
        if (r.item) r.item->setHidden(compact && !checked);
        if (r.check) r.check->setVisible(!compact);
    }
    if (m_levelsBox)
        m_levelsBox->setTitle(tr("Hangforrások"));

    // A VU-doboz alap-láthatósága a nézettől függ: kompaktban (lebegő) felnyitva a
    // kiválasztott eszközök szintjével, teljes nézetben alapból összecsukva (a
    // "▸ Rögzítendő hangforrások módosítása" toggle nyitja). A felhasználó kézi nyitása
    // (m_levelsVisible) felülírja ezt — csak a nézetváltáskor állítjuk az alapot.
    setLevelsVisible(compact);

    updateVoicesLabel();
}

void RecordBar::setLevelsVisible(bool on) {
    m_levelsVisible = on;
    if (m_levelsBox) m_levelsBox->setVisible(on);
    const QString arrow = on ? QStringLiteral("▾") : QStringLiteral("▸");
    if (m_tracksToggle)
        m_tracksToggle->setText(arrow + tr(" Rögzítendő hangforrások módosítása"));
    if (on)
        adjustDeviceListHeight();   // nyitáskor a tartalomra méretezzük a listát
    // FONTOS: a SAJÁT layoutunkat is újra kell számolni a levelsBox rejtése/mutatása
    // után, KÜLÖN updateGeometry-vel — különben a befoglaló (FloatingRecorder) a régi,
    // nagy sizeHint-ünket látja, és nem zsugorodik vissza (sem auto-, sem kézi
    // átméretezéssel, mert a minimumSize is beragad). Ezt kétszintes layoutnál muszáj.
    if (layout())
        layout()->activate();
    updateGeometry();
    // A befoglaló lebegő ablak igazodjon a tartalomhoz (nőjön nyitáskor, zsugorodjon
    // csukáskor — különben üres sáv marad alul, amit kézzel sem lehet eltüntetni).
    emit requestResizeToFit();
}

void RecordBar::adjustDeviceListHeight() {
    if (!m_deviceList)
        return;
    // A LÁTHATÓ sorok együttes magassága (kompakt módban a rejtett sorok nem számítanak),
    // hogy ne kelljen egyszerre átméretezni ÉS görgetni a kis dobozban. Értelmes felső
    // korlát fölött visszakapcsol a görgetés (nehogy a teljes képernyőt elfoglalja).
    int content = 2 * m_deviceList->frameWidth() + 4;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem* it = m_deviceList->item(i);
        if (!it || it->isHidden())
            continue;
        content += m_deviceList->sizeHintForRow(i);
    }
    constexpr int kMaxListHeight = 320;   // e fölött görgessen (sok eszköz esetén)
    const int h = std::min(content, kMaxListHeight);
    m_deviceList->setMinimumHeight(h);
    m_deviceList->setMaximumHeight(h);
}

void RecordBar::updateVoicesLabel() {
    if (!m_voicesLabel)
        return;
    // Rögzítés közben a TÉNYLEGESEN rögzített sávok száma (auto-módban ez nem
    // azonos a bepipált eszközökkel — a valós halmazt a m_recordingDeviceNames tartja).
    if (m_state == tanara::RecordingState::Recording) {
        m_voicesLabel->setText(
            tr("● Rögzítés — %1 hangforrás").arg(m_recordingDeviceNames.size()));
        return;
    }
    // Üresjáratban: a rögzítésre KIVÁLASZTOTT hangforrások száma. (Korábban „N hangot
    // hallok" volt — félrevezető, mert nem hangot detektál, csak a kijelölést számolja.)
    int n = 0;
    for (auto it = m_deviceRows.constBegin(); it != m_deviceRows.constEnd(); ++it)
        if (it.value().check && it.value().check->isChecked())
            ++n;
    m_voicesLabel->setText(n == 0
        ? tr("Nincs kiválasztott hangforrás")
        : tr("🎙 %1 hangforrás kiválasztva").arg(n));
}

void RecordBar::rebuildDeviceList() {
    if (!m_controller || !m_controller->devices())
        return;

    // Az eddigi kijelölés megőrzése NÉV szerint (az eszközök közben átsorszámozódhatnak).
    QStringList previouslyChecked;
    for (auto it = m_deviceRows.constBegin(); it != m_deviceRows.constEnd(); ++it) {
        if (it.value().check && it.value().check->isChecked())
            previouslyChecked << it.key();
    }
    const bool hadRows = !m_deviceRows.isEmpty();

    m_deviceList->clear();
    m_deviceRows.clear();
    m_headerItems.clear();

    const QVector<tanara::AudioDeviceInfo> devs = m_controller->devices()->captureDevices();

    // Előpipálás forrása: ha első felépítés és nem volt korábbi kijelölés, az
    // utoljára használt eszköznevek; ha az is üres → az alapértelmezett eszköz(ök).
    QStringList lastUsed = m_controller->lastUsedDeviceNames();
    const bool useLastUsed = !hadRows && previouslyChecked.isEmpty() && !lastUsed.isEmpty();
    const bool useDefaults = !hadRows && previouslyChecked.isEmpty() && lastUsed.isEmpty();

    // Csoportokba rendezés (Mic / Loopback / Other), mindegyik névre ábécé-sorrendben.
    const tanara::TrackKind kindOrder[] = {
        tanara::TrackKind::Mic, tanara::TrackKind::Loopback, tanara::TrackKind::Other
    };

    auto addHeader = [this](const QString& title) {
        auto* item = new QListWidgetItem(title, m_deviceList);
        item->setFlags(Qt::NoItemFlags);   // nem kijelölhető / nem interaktív
        QFont hf = item->font();
        hf.setBold(true);
        item->setFont(hf);
        m_headerItems.append(item);        // kompakt módban rejthető
        // SZÍN: nincs felülírás → a rendszer-paletta szövegszíne öröklődik (olvasható
        // sötét témán is). Korábban itt 'color: palette(mid)' szürke volt.
    };

    for (tanara::TrackKind kind : kindOrder) {
        // Az aktuális csoport eszközei, névre rendezve.
        QVector<tanara::AudioDeviceInfo> group;
        for (const auto& d : devs)
            if (d.kind == kind)
                group.push_back(d);
        if (group.isEmpty())
            continue;   // pl. "Egyéb" csak akkor jelenik meg, ha van ilyen
        std::sort(group.begin(), group.end(),
                  [](const tanara::AudioDeviceInfo& a, const tanara::AudioDeviceInfo& b) {
                      return a.name.localeAwareCompare(b.name) < 0;
                  });

        addHeader(groupTitle(kind));

        for (const auto& d : group) {
            auto* item = new QListWidgetItem(m_deviceList);

            auto* rowWidget = new QWidget(m_deviceList);
            auto* rl = new QHBoxLayout(rowWidget);
            rl->setContentsMargins(16, 2, 4, 2);   // bal behúzás a csoport-fej alá

            auto* check = new QCheckBox(rowWidget);

            QString nameText = d.name;
            if (d.isDefault)
                nameText += tr("  (alapértelmezett)");
            auto* nameLbl = new QLabel(nameText, rowWidget);
            nameLbl->setMinimumWidth(160);
            // SZÍN: nincs felülírás → öröklött paletta-szövegszín (sötét témán is olvasható).

            auto* level = new QProgressBar(rowWidget);
            level->setRange(0, 100);
            level->setValue(0);
            level->setTextVisible(false);
            level->setMaximumHeight(10);
            level->setMinimumWidth(80);

            rl->addWidget(check);
            rl->addWidget(nameLbl, 1);
            rl->addWidget(level, 1);

            // Előpipálás eldöntése.
            bool checked = false;
            if (useLastUsed)
                checked = lastUsed.contains(d.name);
            else if (useDefaults)
                checked = d.isDefault;
            else
                checked = previouslyChecked.contains(d.name);
            check->setChecked(checked);
            // A programozott setChecked UTÁN kötjük be, hogy az ne mentsen;
            // a felhasználói pipálás viszont AZONNAL perzisztálódjon (két restart közt megmarad).
            connect(check, &QCheckBox::toggled, this, [this](bool) { saveSelection(); });

            item->setSizeHint(rowWidget->sizeHint());
            m_deviceList->setItemWidget(item, rowWidget);

            // Több eszköz is jöhet AZONOS névvel (a headset ~4× felbukkan): az utolsó
            // VU-sávja vezet, de a kijelölés/indítás úgyis név-alapú, így ez rendben van.
            m_deviceRows.insert(d.name, DeviceRow{check, level, item});
        }
    }
    applyViewMode();   // a frissen épített listára is alkalmazzuk az aktuális módot
}

void RecordBar::saveSelection() {
    if (!m_controller)
        return;
    QStringList names;
    for (auto it = m_deviceRows.constBegin(); it != m_deviceRows.constEnd(); ++it)
        if (it.value().check && it.value().check->isChecked())
            names << it.key();
    m_controller->setLastUsedDeviceNames(names);
    updateVoicesLabel();
}

QVector<tanara::AudioDeviceInfo> RecordBar::selectedDevices() const {
    QVector<tanara::AudioDeviceInfo> out;
    if (!m_controller || !m_controller->devices())
        return out;
    const QVector<tanara::AudioDeviceInfo> all = m_controller->devices()->captureDevices();
    for (const auto& d : all) {
        auto it = m_deviceRows.constFind(d.name);
        if (it != m_deviceRows.constEnd() && it.value().check && it.value().check->isChecked())
            out.push_back(d);
    }
    return out;
}

void RecordBar::resetDeviceLevelBars() {
    for (auto it = m_deviceRows.constBegin(); it != m_deviceRows.constEnd(); ++it) {
        if (it.value().level)
            it.value().level->setValue(0);
    }
}

void RecordBar::onStartStopClicked() {
    if (!m_controller)
        return;

    if (m_state == tanara::RecordingState::Recording) {
        m_controller->stopRecording();
        return;
    }
    if (m_state != tanara::RecordingState::Idle)
        return;  // Stopping/Encoding közben nem indítunk

    // A cím szerkesztése épp nyitva lehet (input mód) → előbb véglegesítjük.
    if (m_titleEdit && m_titleEdit->isVisible())
        commitTitleEdit();
    QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty())
        title = tr("Megbeszélés");   // dátum nélkül (a metaadatban ott van)

    // Auto-mód: MINDEN eszközt rögzítünk (a csendeseket a felvétel után eldobjuk);
    // kézi módban a bepipáltakat. A sáv-index → eszköznév leképezést is innen
    // rögzítjük (a sávok ebben a sorrendben jönnek létre) a VU-routinghoz.
    const bool autoAll = m_controller->settings()
                         && m_controller->settings()->settings().autoRecordAllDevices;
    QVector<tanara::AudioDeviceInfo> sel;
    if (autoAll && m_controller->devices())
        sel = m_controller->devices()->autoRecordDevices();   // line-in/AUX kihagyva
    else
        sel = selectedDevices();
    if (sel.isEmpty() && m_controller->devices())   // auto fallback: ha üres a kijelölés
        sel = m_controller->devices()->autoRecordDevices();

    m_recordingDeviceNames.clear();
    for (const auto& d : sel)
        m_recordingDeviceNames << d.name;

    // startRecording() automatikusan leállítja a monitoringot, és elmenti a
    // használt eszközneveket (lastUsedDeviceNames).
    m_controller->startRecording(title, sel);
}

void RecordBar::onDevicesChanged() {
    rebuildDeviceList();
}

void RecordBar::onRecordingStateChanged(tanara::RecordingState state) {
    m_state = state;
    switch (state) {
    case tanara::RecordingState::Idle:
        m_elapsedMs = 0;
        // A cím újra szerkeszthető (✏ visszajön; a szöveg label-ben látszik).
        if (m_editBtn) m_editBtn->show();
        // A checkboxok újra engedélyezve (a listát NEM tiltjuk, hogy a VU látszódjon).
        for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it)
            if (it.value().check) it.value().check->setEnabled(true);
        // Visszatértünk üresjáratba → újraindítjuk az élő szintfigyelést.
        if (m_controller)
            m_controller->startLevelMonitoring();
        break;
    case tanara::RecordingState::Recording:
        // Rögzítés közben a cím nem szerkeszthető: ha épp input módban volt,
        // véglegesítjük, és elrejtjük a ✏-t (marad a label).
        commitTitleEdit();
        if (m_editBtn) m_editBtn->hide();
        // Csak a kijelölést tiltjuk (checkboxok), a lista marad aktív → a VU mozoghat.
        for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it)
            if (it.value().check) it.value().check->setEnabled(false);
        // A monitoringot a controller már leállította; csak a sávokat nullázzuk.
        resetDeviceLevelBars();
        break;
    case tanara::RecordingState::Stopping:
    case tanara::RecordingState::Encoding:
        break;
    }
    updateVoicesLabel();
    updateRecordButton();
}

void RecordBar::updateRecordButton() {
    if (!m_recordBtn)
        return;

    // Egyesített, kétállapotú gomb. Rögzítés közben PIROS, kétsoros
    // "⏹ Leállítás / MM:SS"; üresben semleges "⏺ Felvétel indítása".
    // A REC-jelzést a piros háttér + a "●" pötty adja (a villogó pont funkcióját
    // a piros gomb veszi át — a blink továbbra is a FloatingRecorder pötytyén él).
    switch (m_state) {
    case tanara::RecordingState::Recording: {
        const qint64 totalSec = m_elapsedMs / 1000;
        const QString clock = QStringLiteral("%1:%2")
            .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
            .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
        m_recordBtn->setEnabled(true);
        m_recordBtn->setText(tr("⏹  Leállítás\n%1").arg(clock));
        m_recordBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: #c0392b; color: white; font-weight: bold; "
            "padding: 6px 16px; border: none; border-radius: 5px; }"
            "QPushButton:hover { background: #d04434; }"));
        break;
    }
    case tanara::RecordingState::Idle:
        m_recordBtn->setEnabled(true);
        m_recordBtn->setText(tr("⏺  Felvétel indítása"));
        m_recordBtn->setStyleSheet(QString());   // semleges (rendszer-paletta)
        break;
    case tanara::RecordingState::Stopping:
        m_recordBtn->setEnabled(false);
        m_recordBtn->setText(tr("Leállítás…"));
        m_recordBtn->setStyleSheet(QString());
        break;
    case tanara::RecordingState::Encoding:
        m_recordBtn->setEnabled(false);
        m_recordBtn->setText(tr("Kódolás…"));
        m_recordBtn->setStyleSheet(QString());
        break;
    }
}

void RecordBar::onElapsedChanged(qint64 ms) {
    m_elapsedMs = ms;
    // A számláló a felvétel-gomb 2. sorában frissül (csak rögzítés közben látszik).
    if (m_state == tanara::RecordingState::Recording)
        updateRecordButton();
}

void RecordBar::onLevelMeterUpdated(int trackIndex, float rms) {
    // Felvétel közbeni sáv-szint → a megfelelő eszköz VU-sávjába (nincs külön doboz).
    if (trackIndex < 0 || trackIndex >= m_recordingDeviceNames.size())
        return;
    auto it = m_deviceRows.constFind(m_recordingDeviceNames[trackIndex]);
    if (it == m_deviceRows.constEnd() || !it.value().level)
        return;
    int v = static_cast<int>(rms * 100.0f * kLevelVisualGain / 100.0f);
    v = std::clamp(v, 0, 100);
    it.value().level->setValue(v);
}

void RecordBar::onDeviceLevel(QString deviceName, float rms) {
    auto it = m_deviceRows.constFind(deviceName);
    if (it == m_deviceRows.constEnd() || !it.value().level)
        return;
    int v = static_cast<int>(rms * 100.0f * kLevelVisualGain / 100.0f);
    v = std::clamp(v, 0, 100);
    it.value().level->setValue(v);
}

} // namespace tanara_gui
