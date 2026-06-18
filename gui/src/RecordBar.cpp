#include "RecordBar.h"

#include "tanara/AppController.h"
#include "tanara/audio/DeviceManager.h"

#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QDateTime>
#include <QVariant>

namespace tanara_gui {

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

    topRow->addWidget(m_titleEdit, /*stretch*/ 1);
    topRow->addWidget(m_elapsedLabel);
    topRow->addWidget(m_startStopBtn);
    root->addLayout(topRow);

    // --- szintmérők ---
    auto* metersBox = new QGroupBox(QStringLiteral("Szintek"), this);
    auto* metersOuter = new QVBoxLayout(metersBox);
    m_metersHost = new QWidget(metersBox);
    m_metersLayout = new QVBoxLayout(m_metersHost);
    m_metersLayout->setContentsMargins(0, 0, 0, 0);
    metersOuter->addWidget(m_metersHost);
    root->addWidget(metersBox);

    // --- eszközválasztó ---
    auto* devBox = new QGroupBox(QStringLiteral("Felvevő eszközök"), this);
    auto* devLayout = new QVBoxLayout(devBox);
    m_deviceList = new QListWidget(devBox);
    m_deviceList->setMaximumHeight(120);
    devLayout->addWidget(m_deviceList);
    root->addWidget(devBox);

    connect(m_startStopBtn, &QPushButton::clicked, this, &RecordBar::onStartStopClicked);

    rebuildDeviceList();
    onRecordingStateChanged(m_controller ? m_controller->recordingState()
                                         : tanara::RecordingState::Idle);
}

void RecordBar::rebuildDeviceList() {
    if (!m_controller || !m_controller->devices())
        return;

    // Megjegyezzük a korábbi kijelölést id alapján.
    QStringList previouslyChecked;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* it = m_deviceList->item(i);
        if (it->checkState() == Qt::Checked)
            previouslyChecked << it->data(Qt::UserRole).toString();
    }

    m_deviceList->clear();
    const QVector<tanara::AudioDeviceInfo> devs = m_controller->devices()->captureDevices();
    for (const auto& d : devs) {
        QString label = d.name;
        if (d.kind == tanara::TrackKind::Loopback)
            label += QStringLiteral("  [rendszerhang]");
        else if (d.kind == tanara::TrackKind::Mic)
            label += QStringLiteral("  [mikrofon]");
        if (d.isDefault)
            label += QStringLiteral("  (alapértelmezett)");

        auto* item = new QListWidgetItem(label, m_deviceList);
        item->setData(Qt::UserRole, d.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool wasChecked = previouslyChecked.contains(d.id);
        item->setCheckState((wasChecked || (previouslyChecked.isEmpty() && d.isDefault))
                                ? Qt::Checked : Qt::Unchecked);
    }
}

QVector<tanara::AudioDeviceInfo> RecordBar::selectedDevices() const {
    QVector<tanara::AudioDeviceInfo> out;
    if (!m_controller || !m_controller->devices())
        return out;
    const QVector<tanara::AudioDeviceInfo> all = m_controller->devices()->captureDevices();
    for (int i = 0; i < m_deviceList->count(); ++i) {
        auto* it = m_deviceList->item(i);
        if (it->checkState() != Qt::Checked)
            continue;
        const QString id = it->data(Qt::UserRole).toString();
        for (const auto& d : all) {
            if (d.id == id) { out.push_back(d); break; }
        }
    }
    return out;
}

QProgressBar* RecordBar::meterForTrack(int trackIndex) {
    auto it = m_meters.find(trackIndex);
    if (it != m_meters.end())
        return it.value();

    auto* row = new QWidget(m_metersHost);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    auto* lbl = new QLabel(QStringLiteral("Sáv %1").arg(trackIndex + 1), row);
    lbl->setMinimumWidth(56);
    auto* bar = new QProgressBar(row);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    rl->addWidget(lbl);
    rl->addWidget(bar, 1);
    m_metersLayout->addWidget(row);
    m_meters.insert(trackIndex, bar);
    return bar;
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

    m_controller->startRecording(title, selectedDevices());
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
        m_deviceList->setEnabled(true);
        break;
    case tanara::RecordingState::Recording:
        m_startStopBtn->setEnabled(true);
        m_startStopBtn->setText(QStringLiteral("■ Felvétel leállítása"));
        m_titleEdit->setEnabled(false);
        m_deviceList->setEnabled(false);
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
    int v = static_cast<int>(rms * 100.0f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    meterForTrack(trackIndex)->setValue(v);
}

} // namespace tanara_gui
