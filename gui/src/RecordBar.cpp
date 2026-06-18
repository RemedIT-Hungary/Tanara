#include "RecordBar.h"

#include "tanara/AppController.h"
#include "tanara/audio/DeviceManager.h"

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
#include <QDateTime>
#include <QVariant>
#include <algorithm>

namespace tanara_gui {

namespace {
// "Vizuális erősítés": a halk beszéd is láthatóan mozgassa a sávot.
constexpr float kLevelVisualGain = 140.0f;

QString groupTitle(tanara::TrackKind kind) {
    switch (kind) {
    case tanara::TrackKind::Mic:      return QStringLiteral("🎤 Mikrofonok");
    case tanara::TrackKind::Loopback: return QStringLiteral("🔊 Hangkimenetek");
    default:                          return QStringLiteral("Egyéb");
    }
}
} // namespace

RecordBar::RecordBar(tanara::AppController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller) {

    auto* root = new QVBoxLayout(this);

    // --- felső sor: cím + idő + start/stop ---
    auto* topRow = new QHBoxLayout();

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(QStringLiteral("Megbeszélés címe…"));
    m_titleEdit->setText(QStringLiteral("Megbeszélés %1")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));

    m_elapsedLabel = new QLabel(QStringLiteral("00:00"), this);
    m_elapsedLabel->setMinimumWidth(64);
    m_elapsedLabel->setAlignment(Qt::AlignCenter);
    QFont f = m_elapsedLabel->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    m_elapsedLabel->setFont(f);

    m_startStopBtn = new QPushButton(QStringLiteral("● Felvétel indítása"), this);
    m_startStopBtn->setCheckable(false);

    // Teljes ⇄ Kompakt nézet-váltó.
    m_viewToggleBtn = new QToolButton(this);
    m_viewToggleBtn->setAutoRaise(true);
    m_viewToggleBtn->setText(QStringLiteral("⤡"));
    m_viewToggleBtn->setToolTip(QStringLiteral("Kompakt nézet (csak a kiválasztott eszközök szintje)"));

    topRow->addWidget(m_titleEdit, /*stretch*/ 1);
    topRow->addWidget(m_elapsedLabel);
    topRow->addWidget(m_startStopBtn);
    topRow->addWidget(m_viewToggleBtn);
    root->addLayout(topRow);

    // --- eszközválasztó (élő VU-sávokkal) — a szintek (felvétel előtt ÉS közben) is
    //     itt jelennek meg, eszköznév mellett; nincs külön „Szintek" doboz. ---
    m_devBox = new QGroupBox(QStringLiteral("Hangeszközök"), this);
    auto* devLayout = new QVBoxLayout(m_devBox);
    m_devHint = new QLabel(
        QStringLiteral("Beszélj a mikrofonba / játssz le hangot — a mozgó sáv mutatja, melyik eszköz aktív."),
        m_devBox);
    m_devHint->setWordWrap(true);
    devLayout->addWidget(m_devHint);
    m_deviceList = new QListWidget(m_devBox);
    m_deviceList->setMaximumHeight(200);
    m_deviceList->setSelectionMode(QAbstractItemView::NoSelection);
    devLayout->addWidget(m_deviceList);
    root->addWidget(m_devBox);

    connect(m_startStopBtn, &QPushButton::clicked, this, &RecordBar::onStartStopClicked);
    connect(m_viewToggleBtn, &QToolButton::clicked, this, [this]() {
        setViewMode(m_mode == ViewMode::Full ? ViewMode::Compact : ViewMode::Full);
    });

    rebuildDeviceList();
    onRecordingStateChanged(m_controller ? m_controller->recordingState()
                                         : tanara::RecordingState::Idle);
    applyViewMode();
}

void RecordBar::setViewMode(ViewMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    applyViewMode();
    emit viewModeChanged(mode);
}

void RecordBar::applyViewMode() {
    const bool compact = (m_mode == ViewMode::Compact);
    // Felső sor: kompaktban a cím-mező rejtett (auto-cím megy), az idő + stop marad.
    if (m_titleEdit) m_titleEdit->setVisible(!compact);
    if (m_devHint)   m_devHint->setVisible(!compact);
    if (m_viewToggleBtn) {
        m_viewToggleBtn->setText(compact ? QStringLiteral("⤢") : QStringLiteral("⤡"));
        m_viewToggleBtn->setToolTip(compact
            ? QStringLiteral("Teljes nézet (összes eszköz + beállítás)")
            : QStringLiteral("Kompakt nézet (csak a kiválasztott eszközök szintje)"));
    }
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
    if (m_devBox)
        m_devBox->setTitle(compact ? QStringLiteral("Szintek")
                                   : QStringLiteral("Hangeszközök"));
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
                nameText += QStringLiteral("  (alapértelmezett)");
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

    QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty())
        title = QStringLiteral("Megbeszélés %1")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    // A sáv-index → eszköznév leképezés rögzítése (a sávok a kiválasztott eszközök
    // sorrendjében jönnek létre), hogy a felvétel közbeni szinteket az eszköz
    // VU-sávjába vezethessük.
    const QVector<tanara::AudioDeviceInfo> sel = selectedDevices();
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
        m_startStopBtn->setEnabled(true);
        m_startStopBtn->setText(QStringLiteral("● Felvétel indítása"));
        m_titleEdit->setEnabled(true);
        // A checkboxok újra engedélyezve (a listát NEM tiltjuk, hogy a VU látszódjon).
        for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it)
            if (it.value().check) it.value().check->setEnabled(true);
        // Visszatértünk üresjáratba → újraindítjuk az élő szintfigyelést.
        if (m_controller)
            m_controller->startLevelMonitoring();
        break;
    case tanara::RecordingState::Recording:
        m_startStopBtn->setEnabled(true);
        m_startStopBtn->setText(QStringLiteral("■ Felvétel leállítása"));
        m_titleEdit->setEnabled(false);
        // Csak a kijelölést tiltjuk (checkboxok), a lista marad aktív → a VU mozoghat.
        for (auto it = m_deviceRows.begin(); it != m_deviceRows.end(); ++it)
            if (it.value().check) it.value().check->setEnabled(false);
        // A monitoringot a controller már leállította; csak a sávokat nullázzuk.
        resetDeviceLevelBars();
        break;
    case tanara::RecordingState::Stopping:
        m_startStopBtn->setEnabled(false);
        m_startStopBtn->setText(QStringLiteral("Leállítás…"));
        break;
    case tanara::RecordingState::Encoding:
        m_startStopBtn->setEnabled(false);
        m_startStopBtn->setText(QStringLiteral("Kódolás…"));
        break;
    }
}

void RecordBar::onElapsedChanged(qint64 ms) {
    const qint64 totalSec = ms / 1000;
    const qint64 mm = totalSec / 60;
    const qint64 ss = totalSec % 60;
    m_elapsedLabel->setText(QStringLiteral("%1:%2")
                                .arg(mm, 2, 10, QLatin1Char('0'))
                                .arg(ss, 2, 10, QLatin1Char('0')));
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
