#include "ParticipantsDialog.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QApplication>
#include <QFont>

namespace tanara_gui {

ParticipantsDialog::ParticipantsDialog(tanara::AppController* controller,
                                       const QString& meetingId, QWidget* parent)
    : QDialog(parent), m_controller(controller), m_meetingId(meetingId) {

    setWindowTitle(QStringLiteral("Résztvevők — átírás előtti azonosítás"));
    setModal(true);
    resize(560, 360);

    auto* root = new QVBoxLayout(this);
    m_status = new QLabel(
        QStringLiteral("A hang alapján (lokálisan, Soniox nélkül) keresem, kik voltak a meetingen…"),
        this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* rowsHost = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    root->addWidget(rowsHost, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* saveBtn = buttons->addButton(QStringLiteral("Betanítás + bezárás"),
                                       QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Mégse"), QDialogButtonBox::RejectRole);
    root->addWidget(buttons);
    connect(saveBtn, &QPushButton::clicked, this, &ParticipantsDialog::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (m_controller && m_controller->store())
        m_folder = m_controller->store()->load(meetingId).folder;

    // A nehéz szkennelést a megnyitás UTÁN indítjuk, hogy a dialógus + üzenet látszódjon.
    QTimer::singleShot(0, this, &ParticipantsDialog::runIdentify);
}

void ParticipantsDialog::runIdentify() {
    if (!m_controller)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<tanara::ParticipantGuess> guesses = m_controller->identifyParticipants(m_meetingId);
    QApplication::restoreOverrideCursor();

    const QStringList people = m_controller->knownPeople();

    if (guesses.isEmpty()) {
        m_status->setText(QStringLiteral(
            "Nem találtam azonosítható hangot (nincs betanított modell/lenyomat, "
            "vagy a sávok csendesek). Az átírás után is nevesíthetsz."));
        return;
    }
    m_status->setText(QStringLiteral(
        "Felismert beszélők sávonként. Az ismeretlent (vagy hibásat) nevezd el — ez "
        "betanítja a hangját, és az átírás után automatikusan felismeri. ▶ = meghallgatás."));

    for (const tanara::ParticipantGuess& g : guesses) {
        auto* rowW = new QWidget(this);
        auto* row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);

        auto* play = new QToolButton(rowW);
        play->setText(QStringLiteral("▶"));
        play->setToolTip(QStringLiteral("A felismert beszélő egy mondata"));
        const QString sampleRef = g.sampleRef;
        connect(play, &QToolButton::clicked, this, [this, sampleRef]() { playSample(sampleRef); });

        auto* dev = new QLabel(g.deviceName, rowW);
        dev->setMinimumWidth(150);

        const int pct = static_cast<int>(g.score * 100 + 0.5);
        const QString detLabel = g.name.isEmpty()
            ? QStringLiteral("ismeretlen")
            : QStringLiteral("%1 (%2%)").arg(g.name).arg(pct);
        auto* det = new QLabel(detLabel, rowW);
        det->setMinimumWidth(150);
        if (g.name.isEmpty()) { QFont f = det->font(); f.setItalic(true); det->setFont(f); }

        auto* combo = new QComboBox(rowW);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setMinimumWidth(150);
        combo->addItems(people);
        combo->setCurrentText(g.name);   // ismertnél előtöltjük, ismeretlennél üres
        if (auto* le = combo->lineEdit())
            le->setPlaceholderText(QStringLiteral("név…"));

        row->addWidget(play);
        row->addWidget(dev, 1);
        row->addWidget(det, 1);
        row->addWidget(combo, 1);
        m_rowsLayout->addWidget(rowW);

        m_rows.append(Row{g.trackId, g.sampleRef, g.name, combo});
    }
}

void ParticipantsDialog::onSave() {
    if (!m_controller) { accept(); return; }
    int enrolled = 0;
    for (const Row& r : m_rows) {
        const QString name = r.combo ? r.combo->currentText().trimmed() : QString();
        if (name.isEmpty())
            continue;
        // Csak az új/megváltozott neveket tanítjuk be (a már helyesen felismertet nem
        // duplikáljuk a DB-be).
        if (name == r.detected)
            continue;
        // sampleRef = "track_x.ogg#startMs-endMs"
        qint64 startMs = 0, endMs = 0;
        const int hash = r.sampleRef.indexOf(QLatin1Char('#'));
        if (hash >= 0) {
            const QString range = r.sampleRef.mid(hash + 1);
            const int dash = range.indexOf(QLatin1Char('-'));
            if (dash > 0) { startMs = range.left(dash).toLongLong(); endMs = range.mid(dash + 1).toLongLong(); }
        }
        m_controller->enrollVoiceprintFromSample(name, m_meetingId, r.trackId, startMs, endMs);
        ++enrolled;
    }
    accept();
}

void ParticipantsDialog::playSample(const QString& sampleRef) {
    if (m_folder.isEmpty() || sampleRef.isEmpty())
        return;
    QString file = sampleRef;
    qint64 startMs = 0, endMs = 0;
    const int hash = sampleRef.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        file = sampleRef.left(hash);
        const QString range = sampleRef.mid(hash + 1);
        const int dash = range.indexOf(QLatin1Char('-'));
        if (dash > 0) { startMs = range.left(dash).toLongLong(); endMs = range.mid(dash + 1).toLongLong(); }
    }
    const QString path = QDir(m_folder).filePath(file);
    if (!QFileInfo::exists(path))
        return;
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOutput);
        connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (m_playEndMs > 0 && pos >= m_playEndMs) m_player->pause();
        });
    }
    m_playEndMs = endMs;
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(path));
    if (startMs > 0) {
        auto* c = new QMetaObject::Connection();
        *c = connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
                     [this, startMs, c](QMediaPlayer::MediaStatus st) {
            if (st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia) {
                m_player->setPosition(startMs);
                m_player->play();
                disconnect(*c); delete c;
            }
        });
    } else {
        m_player->play();
    }
}

} // namespace tanara_gui
