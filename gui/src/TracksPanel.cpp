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
    m_restoreBtn = new QPushButton(QStringLiteral("Visszaállítás"), this);
    m_deleteBtn  = new QPushButton(QStringLiteral("🗑 Törlés (végleges)"), this);
    btnRow->addWidget(m_restoreBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &TracksPanel::updateButtons);
    connect(m_restoreBtn, &QPushButton::clicked, this, &TracksPanel::onRestoreClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TracksPanel::onDeleteClicked);
    updateButtons();
}

void TracksPanel::setController(tanara::AppController* controller) {
    m_controller = controller;
}

void TracksPanel::setMeeting(const tanara::Meeting& meeting) {
    m_meetingId = meeting.id;
    m_tracks = meeting.tracks;
    populate();
}

void TracksPanel::clearMeeting() {
    m_meetingId.clear();
    m_tracks.clear();
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
    // Visszaállítás csak eldobott sávra; törlés bármelyikre.
    m_restoreBtn->setEnabled(hasSel && !active);
    m_deleteBtn->setEnabled(hasSel);
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

    m_controller->deleteTrack(m_meetingId, item->data(RoleTrackId).toString());
}

} // namespace tanara_gui
