#include "TranscriptPlayer.h"

#include "tanara/AppController.h"

#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QBrush>
#include <QStringList>
#include <QSet>

#include <QMediaPlayer>
#include <QAudioOutput>

namespace tanara_gui {

static constexpr int kRoleStartMs = Qt::UserRole;
static constexpr int kRoleEndMs   = Qt::UserRole + 1;
static constexpr int kRoleRawSpeaker = Qt::UserRole + 2;   // a sor NYERS beszélő-címkéje

TranscriptPlayer::TranscriptPlayer(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // --- lejátszó-sáv ---
    auto* bar = new QHBoxLayout();
    m_playPauseBtn = new QPushButton(QStringLiteral("▶ Lejátszás"), this);
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setRange(0, 0);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_timeLabel->setMinimumWidth(110);

    bar->addWidget(m_playPauseBtn);
    bar->addWidget(m_seekSlider, 1);
    bar->addWidget(m_timeLabel);
    root->addLayout(bar);

    // --- beszélők sora (átnevezhető combo-k) ---
    auto* speakersRow = new QHBoxLayout();
    m_speakersLabel = new QLabel(QStringLiteral("Beszélők:"), this);
    speakersRow->addWidget(m_speakersLabel);
    m_speakersPanel = new QWidget(this);
    m_speakersLayout = new QHBoxLayout(m_speakersPanel);
    m_speakersLayout->setContentsMargins(0, 0, 0, 0);
    speakersRow->addWidget(m_speakersPanel, 1);
    speakersRow->addStretch(0);
    root->addLayout(speakersRow);
    m_speakersLabel->setVisible(false);
    m_speakersPanel->setVisible(false);

    // --- szegmens-lista ---
    m_list = new QListWidget(this);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(false);
    m_list->setTextElideMode(Qt::ElideNone);
    root->addWidget(m_list, 1);

    connect(m_playPauseBtn, &QPushButton::clicked,
            this, &TranscriptPlayer::onPlayPauseClicked);
    connect(m_seekSlider, &QSlider::sliderPressed,
            this, &TranscriptPlayer::onSliderPressed);
    connect(m_seekSlider, &QSlider::sliderReleased,
            this, &TranscriptPlayer::onSliderReleased);
    connect(m_seekSlider, &QSlider::sliderMoved,
            this, &TranscriptPlayer::onSliderMoved);
    connect(m_list, &QListWidget::itemClicked,
            this, &TranscriptPlayer::onItemClicked);

    setBarEnabled(false);
}

TranscriptPlayer::~TranscriptPlayer() {
    if (m_player)
        m_player->stop();
}

void TranscriptPlayer::ensurePlayer() {
    if (m_player)
        return;
    // Lusta létrehozás — csak az első forrás-betöltéskor, hogy az app
    // indítása ne triggerelje a Qt Multimedia hwaccel-próbáit.
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    connect(m_player, &QMediaPlayer::durationChanged,
            this, &TranscriptPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::positionChanged,
            this, &TranscriptPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &TranscriptPlayer::onPlaybackStateChanged);
}

void TranscriptPlayer::setBarEnabled(bool on) {
    m_playPauseBtn->setEnabled(on);
    m_seekSlider->setEnabled(on);
}

QString TranscriptPlayer::formatTime(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSec = ms / 1000;
    const qint64 mm = totalSec / 60;
    const qint64 ss = totalSec % 60;
    return QStringLiteral("%1:%2")
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'));
}

void TranscriptPlayer::updateTimeLabel(qint64 pos, qint64 dur) {
    m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") + formatTime(dur));
}

bool TranscriptPlayer::loadSegments(const QString& path) {
    m_segments.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    const QJsonArray arr = doc.array();
    m_segments.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        Segment s;
        s.startMs = static_cast<qint64>(o.value(QStringLiteral("startMs")).toDouble());
        s.endMs   = static_cast<qint64>(o.value(QStringLiteral("endMs")).toDouble());
        s.speaker = o.value(QStringLiteral("speaker")).toString();
        s.text    = o.value(QStringLiteral("text")).toString();
        m_segments.push_back(s);
    }
    return true;
}

QString TranscriptPlayer::displayName(const QString& rawSpeaker) const {
    return m_speakerMap.value(rawSpeaker, rawSpeaker);
}

void TranscriptPlayer::populateList() {
    m_list->clear();
    m_highlightedRow = -1;
    for (const Segment& s : m_segments) {
        // [mm:ss]  <SPEAKER>  <text> — a beszélőt a speakerMap szerinti valódi
        // névvel jelenítjük meg (ha van), nagybetűvel kiemelve. A NYERS címkét
        // az item-adatban őrizzük, hogy az átnevezés azt tudja célozni.
        const QString ts = QStringLiteral("[%1]").arg(formatTime(s.startMs));
        QString line = ts;
        if (!s.speaker.isEmpty())
            line += QStringLiteral("  %1").arg(displayName(s.speaker).toUpper());
        line += QStringLiteral("  %1").arg(s.text);

        auto* item = new QListWidgetItem(line, m_list);
        item->setData(kRoleStartMs, static_cast<qlonglong>(s.startMs));
        item->setData(kRoleEndMs,   static_cast<qlonglong>(s.endMs));
        item->setData(kRoleRawSpeaker, s.speaker);
    }
}

void TranscriptPlayer::populateSpeakersPanel() {
    // Régi combo-k eltakarítása.
    QLayoutItem* old = nullptr;
    while ((old = m_speakersLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = old->widget())
            w->deleteLater();
        delete old;
    }

    // Egyedi NYERS beszélő-címkék a szegmensekből, az első előfordulás sorrendjében.
    QStringList rawSpeakers;
    QSet<QString> seen;
    for (const Segment& s : m_segments) {
        if (s.speaker.isEmpty() || seen.contains(s.speaker))
            continue;
        seen.insert(s.speaker);
        rawSpeakers.push_back(s.speaker);
    }

    const QStringList people = m_controller ? m_controller->knownPeople() : QStringList{};

    for (const QString& raw : rawSpeakers) {
        auto* lbl = new QLabel(raw + QStringLiteral(":"), m_speakersPanel);
        auto* combo = new QComboBox(m_speakersPanel);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setMinimumWidth(120);
        combo->addItems(people);
        combo->setCurrentText(displayName(raw));
        if (auto* c = combo->completer())
            c->setCaseSensitivity(Qt::CaseInsensitive);

        // Commit: legördülőből választás VAGY a szerkesztés befejezése.
        connect(combo, &QComboBox::activated, this,
                [this, raw, combo](int) { onSpeakerRenamed(raw, combo->currentText()); });
        if (auto* le = combo->lineEdit())
            connect(le, &QLineEdit::editingFinished, this,
                    [this, raw, combo]() { onSpeakerRenamed(raw, combo->currentText()); });

        m_speakersLayout->addWidget(lbl);
        m_speakersLayout->addWidget(combo);
    }
    m_speakersLayout->addStretch(1);

    const bool any = !rawSpeakers.isEmpty();
    m_speakersLabel->setVisible(any);
    m_speakersPanel->setVisible(any);
}

void TranscriptPlayer::onSpeakerRenamed(const QString& rawLabel, const QString& chosenName) {
    if (!m_controller || m_meetingId.isEmpty())
        return;
    const QString chosen = chosenName.trimmed();
    // Nincs változás → ne hívjuk feleslegesen a backendet (nem-üres azonos név).
    if (chosen == displayName(rawLabel))
        return;
    m_controller->renameSpeaker(m_meetingId, rawLabel, chosen);
    // A frissítést a speakerMapChanged → újratöltés intézi (MainWindow).
}

void TranscriptPlayer::setController(tanara::AppController* controller) {
    m_controller = controller;
}

void TranscriptPlayer::loadMeeting(const tanara::Meeting& meeting,
                                   const QString& audioPath) {
    m_singleSegmentMode = false;
    m_singleSegmentEndMs = 0;
    m_userSeeking = false;

    m_meetingId = meeting.id;
    m_speakerMap = meeting.speakerMap;
    const QString segmentsJsonPath =
        QDir(meeting.folder).filePath(QStringLiteral("transcript.segments.json"));

    // Forrás-váltáskor megállítjuk a korábbi lejátszást.
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_playPauseBtn->setText(QStringLiteral("▶ Lejátszás"));
    m_seekSlider->setRange(0, 0);
    updateTimeLabel(0, 0);

    const bool haveSegments =
        QFileInfo::exists(segmentsJsonPath) && loadSegments(segmentsJsonPath);

    if (!haveSegments) {
        m_segments.clear();
        m_list->clear();
        m_list->addItem(
            QStringLiteral("Nincs átirat — futtass Átírást."));
        populateSpeakersPanel();   // nincs szegmens → üres panel, elrejti magát
        setBarEnabled(false);
        m_hasAudio = false;
        m_audioPath.clear();
        return;
    }

    populateList();
    populateSpeakersPanel();

    m_hasAudio = !audioPath.isEmpty() && QFileInfo::exists(audioPath);
    m_audioPath = m_hasAudio ? audioPath : QString();
    setBarEnabled(m_hasAudio);

    if (m_hasAudio) {
        ensurePlayer();
        m_player->setSource(QUrl::fromLocalFile(m_audioPath));   // betölt, NEM játszik
    }
}

void TranscriptPlayer::clearMeeting() {
    m_singleSegmentMode = false;
    m_userSeeking = false;
    m_meetingId.clear();
    m_speakerMap.clear();
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_segments.clear();
    m_list->clear();
    populateSpeakersPanel();
    m_seekSlider->setRange(0, 0);
    updateTimeLabel(0, 0);
    m_playPauseBtn->setText(QStringLiteral("▶ Lejátszás"));
    setBarEnabled(false);
    m_hasAudio = false;
    m_audioPath.clear();
    m_highlightedRow = -1;
}

void TranscriptPlayer::onPlayPauseClicked() {
    if (!m_hasAudio)
        return;
    ensurePlayer();
    // Normál Play → kilépünk az egy-szegmens módból (folyamatos lejátszás).
    m_singleSegmentMode = false;
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

void TranscriptPlayer::onDurationChanged(qint64 dur) {
    m_seekSlider->setRange(0, static_cast<int>(dur));
    updateTimeLabel(m_player ? m_player->position() : 0, dur);
}

void TranscriptPlayer::onPositionChanged(qint64 pos) {
    if (!m_userSeeking)
        m_seekSlider->setValue(static_cast<int>(pos));
    updateTimeLabel(pos, m_player ? m_player->duration() : 0);

    // Egy-szegmens lejátszás: a cél-szegmens végén megállunk.
    if (m_singleSegmentMode && pos >= m_singleSegmentEndMs) {
        m_singleSegmentMode = false;
        if (m_player)
            m_player->pause();
    }

    highlightForPosition(pos);
}

void TranscriptPlayer::highlightForPosition(qint64 pos) {
    if (m_segments.isEmpty())
        return;
    // Megkeressük a szegmenst, amelyre: startMs <= pos < (következő.startMs vagy endMs).
    int found = -1;
    for (int i = 0; i < m_segments.size(); ++i) {
        const qint64 start = m_segments[i].startMs;
        const qint64 upper = (i + 1 < m_segments.size())
                                 ? m_segments[i + 1].startMs
                                 : m_segments[i].endMs;
        if (pos >= start && pos < upper) {
            found = i;
            break;
        }
    }
    if (found < 0 || found == m_highlightedRow)
        return;

    // Korábbi kiemelés visszaállítása.
    if (m_highlightedRow >= 0 && m_highlightedRow < m_list->count()) {
        if (auto* prev = m_list->item(m_highlightedRow))
            prev->setBackground(QBrush());
    }
    if (auto* cur = m_list->item(found)) {
        cur->setBackground(QColor(255, 244, 200));   // halvány sárga kiemelés
        m_list->setCurrentRow(found);
        m_list->scrollToItem(cur, QAbstractItemView::PositionAtCenter);
    }
    m_highlightedRow = found;
}

void TranscriptPlayer::onPlaybackStateChanged() {
    if (!m_player)
        return;
    const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_playPauseBtn->setText(playing ? QStringLiteral("⏸ Szünet")
                                    : QStringLiteral("▶ Lejátszás"));
}

void TranscriptPlayer::onSliderPressed() {
    m_userSeeking = true;
}

void TranscriptPlayer::onSliderReleased() {
    if (m_player)
        m_player->setPosition(m_seekSlider->value());
    m_userSeeking = false;
    // Kézi tekerés megszakítja az egy-szegmens módot.
    m_singleSegmentMode = false;
}

void TranscriptPlayer::onSliderMoved(int value) {
    // Húzás közben azonnali seek + címke-frissítés (a feedback ellen m_userSeeking véd).
    if (m_player)
        m_player->setPosition(value);
    updateTimeLabel(value, m_player ? m_player->duration() : 0);
}

void TranscriptPlayer::onItemClicked(QListWidgetItem* item) {
    if (!item || !m_hasAudio)
        return;
    bool okStart = false, okEnd = false;
    const qint64 startMs = item->data(kRoleStartMs).toLongLong(&okStart);
    const qint64 endMs   = item->data(kRoleEndMs).toLongLong(&okEnd);
    if (!okStart)
        return;

    ensurePlayer();
    const bool wasPlaying =
        m_player->playbackState() == QMediaPlayer::PlayingState;

    m_player->setPosition(startMs);
    if (wasPlaying) {
        // Folyamatos lejátszás — csak ugrunk, az egy-szegmens módot elhagyjuk.
        m_singleSegmentMode = false;
    } else {
        // Állt → lejátsszuk ezt az egy mondatot, a végén (endMs) megállunk.
        m_singleSegmentMode = okEnd && endMs > startMs;
        m_singleSegmentEndMs = endMs;
        m_player->play();
    }
}

} // namespace tanara_gui
