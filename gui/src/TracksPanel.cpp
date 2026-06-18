#include "TracksPanel.h"

#include "tanara/AppController.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFont>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QDir>
#include <QFileInfo>

namespace tanara_gui {

namespace {
constexpr int RoleTrackId = Qt::UserRole;
constexpr int RoleActive  = Qt::UserRole + 1;

QString kindStr(tanara::TrackKind k) {
    switch (k) {
    case tanara::TrackKind::Mic:      return QStringLiteral("🎤 mikrofon");
    case tanara::TrackKind::Loopback: return QStringLiteral("🔊 hangkimenet");
    default:                          return QStringLiteral("egyéb");
    }
}
} // namespace

TracksPanel::TracksPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    m_hint = new QLabel(
        QStringLiteral("A felvétel sávjai. A csendesnek ítélt sávok automatikusan "
                       "„eldobott” állapotba kerülnek (a fájl megmarad). Visszaállíthatod, "
                       "vagy véglegesen törölheted (fájllal együtt)."), this);
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout();
    m_playBtn = new QPushButton(QStringLiteral("▶ Meghallgatás"), this);
    m_playBtn->setToolTip(QStringLiteral("A kijelölt sáv lejátszása (eldobott sávé is, "
                                         "hogy törlés előtt ellenőrizhető legyen)."));
    m_stopBtn = new QPushButton(QStringLiteral("⏹"), this);
    m_restoreBtn = new QPushButton(QStringLiteral("Visszaállítás"), this);
    m_deleteBtn  = new QPushButton(QStringLiteral("🗑 Törlés (végleges)"), this);
    m_remixBtn   = new QPushButton(QStringLiteral("🔀 Lekeverés frissítése"), this);
    m_remixBtn->setToolTip(QStringLiteral(
        "A kevert hang (mixdown.mp3) újragenerálása az AKTÍV sávokból — a lejátszó "
        "ezt használja. Sáv eldobása/törlése után érdemes frissíteni."));
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addWidget(m_restoreBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(m_remixBtn);
    root->addLayout(btnRow);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &TracksPanel::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlayClicked(); });
    connect(m_playBtn, &QPushButton::clicked, this, &TracksPanel::onPlayClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &TracksPanel::onStopClicked);
    connect(m_restoreBtn, &QPushButton::clicked, this, &TracksPanel::onRestoreClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TracksPanel::onDeleteClicked);
    connect(m_remixBtn, &QPushButton::clicked, this, &TracksPanel::onRemixClicked);
    updateButtons();
}

void TracksPanel::setController(tanara::AppController* controller) {
    m_controller = controller;
}

void TracksPanel::setMeeting(const tanara::Meeting& meeting) {
    m_meetingId = meeting.id;
    m_folder = meeting.folder;
    m_tracks = meeting.tracks;
    m_mixdownDirty = meeting.mixdownDirty;
    m_remixRunning = false;   // friss meeting-állapot → ha futott, mostanra kész
    onStopClicked();
    populate();
}

void TracksPanel::clearMeeting() {
    m_meetingId.clear();
    m_folder.clear();
    m_tracks.clear();
    m_mixdownDirty = false;
    m_remixRunning = false;
    onStopClicked();
    populate();
}

void TracksPanel::populate() {
    m_list->clear();
    for (const tanara::Track& t : m_tracks) {
        QString label = QStringLiteral("%1  (%2)").arg(t.deviceName, kindStr(t.kind));
        if (!t.active)
            label += QStringLiteral("  —  ELDOBOTT (csendes)");
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(RoleTrackId, t.id);
        item->setData(RoleActive, t.active);
        if (!t.active) {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
            item->setForeground(Qt::gray);
        }
    }
    updateButtons();
}

void TracksPanel::updateButtons() {
    auto* item = m_list->currentItem();
    const bool hasSel = item != nullptr;
    const bool active = hasSel && item->data(RoleActive).toBool();
    // Visszaállítás csak eldobott sávra; törlés/lejátszás bármelyikre.
    m_restoreBtn->setEnabled(hasSel && !active);
    m_deleteBtn->setEnabled(hasSel);
    m_playBtn->setEnabled(hasSel);

    // „Lekeverés frissítése": akkor van értelme, ha van meeting + (több) sáv. Ha a
    // mixdown elavult (dirty), kiemeljük (félkövér + jelölés). Futás közben tiltva.
    const bool canRemix = !m_meetingId.isEmpty() && !m_tracks.isEmpty();
    m_remixBtn->setEnabled(canRemix && !m_remixRunning);
    QFont rf = m_remixBtn->font();
    rf.setBold(m_mixdownDirty && !m_remixRunning);
    m_remixBtn->setFont(rf);
    m_remixBtn->setText(m_remixRunning
        ? QStringLiteral("⏳ Lekeverés folyamatban…")
        : (m_mixdownDirty ? QStringLiteral("🔀 Lekeverés frissítése (elavult)")
                          : QStringLiteral("🔀 Lekeverés frissítése")));
}

void TracksPanel::onRemixClicked() {
    if (!m_controller || m_meetingId.isEmpty() || m_remixRunning)
        return;
    // A lejátszót elengedjük (a régi mixdown fájlja nyitva lehet → felülírható legyen),
    // majd elindítjuk az aszinkron újrakeverést. A panel a tracksChanged jelre frissül
    // (a MainWindow újratölti a meetinget → setMeeting → m_remixRunning visszaáll).
    onStopClicked();
    m_remixRunning = true;
    updateButtons();
    m_controller->regenerateMixdown(m_meetingId);
}

void TracksPanel::onPlayClicked() {
    auto* item = m_list->currentItem();
    if (!item || m_folder.isEmpty())
        return;
    // A kijelölt sávhoz tartozó fájl megkeresése (id alapján).
    const QString trackId = item->data(RoleTrackId).toString();
    QString file;
    for (const tanara::Track& t : m_tracks)
        if (t.id == trackId) { file = t.file; break; }
    if (file.isEmpty())
        return;
    const QString path = QDir(m_folder).filePath(file);
    if (!QFileInfo::exists(path)) {
        QMessageBox::information(this, QStringLiteral("Meghallgatás"),
            QStringLiteral("A hangsáv-fájl nem található:\n%1").arg(path));
        return;
    }
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOutput);
    }
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

void TracksPanel::onStopClicked() {
    if (m_player)
        m_player->stop();
}

void TracksPanel::onRestoreClicked() {
    auto* item = m_list->currentItem();
    if (!item || !m_controller || m_meetingId.isEmpty())
        return;
    m_controller->restoreTrack(m_meetingId, item->data(RoleTrackId).toString());
    // A panelt a MainWindow frissíti a tracksChanged jelre.
}

void TracksPanel::onDeleteClicked() {
    auto* item = m_list->currentItem();
    if (!item || !m_controller || m_meetingId.isEmpty())
        return;

    const auto ans = QMessageBox::question(
        this, QStringLiteral("Sáv törlése"),
        QStringLiteral("Véglegesen törlöd ezt a hangsávot? A hangfájl fizikailag törlődik, "
                       "ez nem visszavonható."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    // A fájl fizikai törléséhez Windowson NEM lehet nyitva → elengedjük a lejátszó
    // forrását (ha épp ezt a sávot hallgattuk).
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_controller->deleteTrack(m_meetingId, item->data(RoleTrackId).toString());
}

} // namespace tanara_gui
