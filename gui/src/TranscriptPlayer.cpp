#include "TranscriptPlayer.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"

#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextEdit>          // QTextEdit::ExtraSelection
#include <QTextBlock>
#include <QTextCursor>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QFrame>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QStringList>
#include <QSet>
#include <QMouseEvent>
#include <QEvent>

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QDebug>

#include <algorithm>   // std::upper_bound, std::is_sorted, std::stable_sort

namespace tanara_gui {

TranscriptPlayer::TranscriptPlayer(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // --- lejátszó-sáv (KÜLÖN widget, hogy a MainWindow kiemelhesse a fülek alá) ---
    // A Könyvtár-otthon mockupban a médialejátszó MINDIG látható, a jobb pane alján.
    // Itt felépítjük, de a MainWindow reparentálhatja; a logika (seek/highlight) itt marad.
    m_playerBar = new QWidget(this);
    auto* bar = new QHBoxLayout(m_playerBar);
    bar->setContentsMargins(0, 0, 0, 0);
    m_playPauseBtn = new QPushButton(QStringLiteral("▶ Lejátszás"), m_playerBar);
    m_seekSlider = new QSlider(Qt::Horizontal, m_playerBar);
    m_seekSlider->setRange(0, 0);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), m_playerBar);
    m_timeLabel->setMinimumWidth(110);

    // Hangerő-csúszka (a QAudioOutput->setVolume lineáris 0..1; itt 0..100 → /100).
    auto* volIcon = new QLabel(QStringLiteral("🔊"), m_playerBar);
    m_volumeSlider = new QSlider(Qt::Horizontal, m_playerBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);   // unity (a QAudioOutput 1.0 = nincs app-oldali csillapítás)
    m_volumeSlider->setMaximumWidth(90);
    m_volumeSlider->setToolTip(QStringLiteral("Hangerő"));

    bar->addWidget(m_playPauseBtn);
    bar->addWidget(m_seekSlider, 1);
    bar->addWidget(m_timeLabel);
    bar->addWidget(volIcon);
    bar->addWidget(m_volumeSlider);
    root->addWidget(m_playerBar);

    // --- beszélők: KOMPAKT legenda-chipek (ki van a meetingen) ---
    auto* speakersRow = new QHBoxLayout();
    m_speakersLabel = new QLabel(QStringLiteral("Beszélők:"), this);
    speakersRow->addWidget(m_speakersLabel);
    m_legendPanel = new QWidget(this);
    m_legendLayout = new QHBoxLayout(m_legendPanel);
    m_legendLayout->setContentsMargins(0, 0, 0, 0);
    m_legendLayout->setSpacing(6);
    speakersRow->addWidget(m_legendPanel, 1);
    root->addLayout(speakersRow);
    m_speakersLabel->setVisible(false);
    m_legendPanel->setVisible(false);

    // --- KONTEXTUÁLIS hozzárendelés-sáv (alapból rejtett; kijelöléskor / chip-kattintásra) ---
    // „🖱 „<beszélő>" — ki mondta?  [név ▾] [Hozzárendel] [▶] [🔍] … [✕]"
    m_assignBar = new QFrame(this);
    m_assignBar->setObjectName(QStringLiteral("assignBar"));
    m_assignBar->setFrameShape(QFrame::StyledPanel);
    m_assignBar->setStyleSheet(QStringLiteral(
        "QFrame#assignBar { background: rgba(227,179,74,0.18); "
        "border: 1px solid rgba(227,179,74,0.55); border-radius: 6px; }"));
    auto* ab = new QHBoxLayout(m_assignBar);
    ab->setContentsMargins(8, 4, 8, 4);
    m_assignLabel = new QLabel(QStringLiteral("🖱 Jelölj ki egy részt — ki mondta?"), m_assignBar);
    m_assignCombo = new QComboBox(m_assignBar);
    m_assignCombo->setEditable(true);
    m_assignCombo->setInsertPolicy(QComboBox::NoInsert);
    m_assignCombo->setMinimumWidth(160);
    if (auto* le = m_assignCombo->lineEdit())
        le->setPlaceholderText(QStringLiteral("név megadása…"));
    if (auto* c = m_assignCombo->completer())
        c->setCaseSensitivity(Qt::CaseInsensitive);
    auto* assignApply  = new QPushButton(QStringLiteral("Hozzárendel"), m_assignBar);
    auto* assignListen = new QToolButton(m_assignBar);
    assignListen->setText(QStringLiteral("▶"));
    assignListen->setAutoRaise(true);
    assignListen->setToolTip(QStringLiteral("A beszélő egy mondatának meghallgatása"));
    auto* assignTest   = new QToolButton(m_assignBar);
    assignTest->setText(QStringLiteral("🔍"));
    assignTest->setAutoRaise(true);
    assignTest->setToolTip(QStringLiteral("Fingerprint-teszt: a legjobb egyezés a személy-adatbázisból"));
    auto* assignClose  = new QToolButton(m_assignBar);
    assignClose->setText(QStringLiteral("✕"));
    assignClose->setAutoRaise(true);
    ab->addWidget(m_assignLabel);
    ab->addWidget(m_assignCombo, 1);
    ab->addWidget(assignApply);
    ab->addWidget(assignListen);
    ab->addWidget(assignTest);
    ab->addStretch(0);
    ab->addWidget(assignClose);
    root->addWidget(m_assignBar);
    m_assignBar->setVisible(false);

    auto applyAssign = [this]() {
        if (m_assignRaw.isEmpty()) return;
        onSpeakerRenamed(m_assignRaw, m_assignCombo->currentText());
        hideAssignBar();
    };
    connect(assignApply, &QPushButton::clicked, this, applyAssign);
    if (auto* le = m_assignCombo->lineEdit())
        connect(le, &QLineEdit::returnPressed, this, applyAssign);
    connect(assignListen, &QToolButton::clicked, this,
            [this]() { if (!m_assignRaw.isEmpty()) playSpeakerSample(m_assignRaw); });
    connect(assignTest, &QToolButton::clicked, this,
            [this]() { if (!m_assignRaw.isEmpty()) onTestSpeaker(m_assignRaw); });
    connect(assignClose, &QToolButton::clicked, this, [this]() { hideAssignBar(); });

    // --- szegmens-nézet (QPlainTextEdit, szegmensenként egy blokk) ---
    // Csak olvasható, de egérrel kijelölhető/másolható; a kattintásokat a viewportra
    // tett eventFilter fogja (kattintott blokk → szegmens → odaugrás). A dokumentum-
    // motor csak a látható blokkokat rendereli → hosszú átiratnál is sima.
    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setCenterOnScroll(true);
    m_view->viewport()->installEventFilter(this);
    root->addWidget(m_view, 1);

    // Kijelölés az átiratban → a kijelölt rész beszélőjére előhozza a hozzárendelés-sávot.
    connect(m_view, &QPlainTextEdit::selectionChanged,
            this, &TranscriptPlayer::onTranscriptSelectionChanged);

    connect(m_playPauseBtn, &QPushButton::clicked,
            this, &TranscriptPlayer::onPlayPauseClicked);
    connect(m_seekSlider, &QSlider::sliderPressed,
            this, &TranscriptPlayer::onSliderPressed);
    connect(m_seekSlider, &QSlider::sliderReleased,
            this, &TranscriptPlayer::onSliderReleased);
    connect(m_seekSlider, &QSlider::sliderMoved,
            this, &TranscriptPlayer::onSliderMoved);
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &TranscriptPlayer::onVolumeChanged);

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
    // A csúszka aktuális állását azonnal alkalmazzuk (a sáv a player előtt jön létre).
    if (m_volumeSlider)
        m_audioOutput->setVolume(m_volumeSlider->value() / 100.0f);

    connect(m_player, &QMediaPlayer::durationChanged,
            this, &TranscriptPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::positionChanged,
            this, &TranscriptPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &TranscriptPlayer::onPlaybackStateChanged);
    // Diagnosztika: ha a hang-kimenet/dekódolás hibázik (pl. sink-választás), írjuk ki
    // a stderr-re — így terminálból indítva látszik, miért néma a lejátszás.
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [](QMediaPlayer::Error e, const QString& s) {
                qWarning() << "[Tanara] QMediaPlayer error:" << e << s;
            });
    qInfo().noquote() << "[Tanara] audio-out eszköz:" << m_audioOutput->device().description()
                      << "| volume:" << m_audioOutput->volume();
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
    m_segmentStarts.clear();
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

    // A bináris keresés időrendet feltételez. A forrás jellemzően rendezett, de
    // ha mégsem, egyszer (betöltéskor) startMs szerint rendezünk, hogy a
    // highlightForPosition() helyesen működjön. Csak akkor rendezünk, ha kell.
    const bool sorted = std::is_sorted(
        m_segments.cbegin(), m_segments.cend(),
        [](const Segment& a, const Segment& b) { return a.startMs < b.startMs; });
    if (!sorted) {
        std::stable_sort(
            m_segments.begin(), m_segments.end(),
            [](const Segment& a, const Segment& b) { return a.startMs < b.startMs; });
    }

    // A startMs-ek párhuzamos vektora a bináris kereséshez (upper_bound).
    m_segmentStarts.reserve(m_segments.size());
    for (const Segment& s : m_segments)
        m_segmentStarts.push_back(s.startMs);

    return true;
}

QString TranscriptPlayer::displayName(const QString& rawSpeaker) const {
    return m_speakerMap.value(rawSpeaker, rawSpeaker);
}

void TranscriptPlayer::populateList() {
    m_highlightedRow = -1;
    // Egy szegmens = egy blokk (bekezdés). A blokk-index == szegmens-index, így a
    // kiemeléshez/kattintáshoz nem kell külön item-adat. Egyetlen setPlainText →
    // a dokumentum egyszer épül fel (gyors), nincs per-item layout-vihar.
    QStringList lines;
    lines.reserve(m_segments.size());
    for (const Segment& s : m_segments) {
        // [mm:ss]  <SPEAKER>  <text> — a beszélőt a speakerMap szerinti valódi
        // névvel jelenítjük meg (ha van), nagybetűvel kiemelve.
        QString line = QStringLiteral("[%1]").arg(formatTime(s.startMs));
        if (!s.speaker.isEmpty())
            line += QStringLiteral("  %1").arg(displayName(s.speaker).toUpper());
        line += QStringLiteral("  %1").arg(s.text);
        lines.push_back(line);
    }
    m_view->setPlainText(lines.join(QLatin1Char('\n')));
    m_view->moveCursor(QTextCursor::Start);
    m_view->setExtraSelections({});
}

void TranscriptPlayer::rebuildLegend() {
    // Régi chipek eltakarítása.
    QLayoutItem* old = nullptr;
    while ((old = m_legendLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = old->widget())
            w->deleteLater();
        delete old;
    }
    hideAssignBar();

    // Egyedi NYERS beszélő-címkék a szegmensekből, az első előfordulás sorrendjében.
    QStringList rawSpeakers;
    QSet<QString> seen;
    for (const Segment& s : m_segments) {
        if (s.speaker.isEmpty() || seen.contains(s.speaker))
            continue;
        seen.insert(s.speaker);
        rawSpeakers.push_back(s.speaker);
    }

    const QString ownName = (m_controller && m_controller->settings())
        ? m_controller->settings()->settings().userSpeakerName.trimmed()
        : QString();

    for (const QString& raw : rawSpeakers) {
        const QString disp = displayName(raw);
        const bool isOwn = !ownName.isEmpty() && disp == ownName;
        // „ismeretlen" = még nincs valódi név (a megjelenített == a nyers címke, pl. „Távoli 1").
        const bool unknown = (disp == raw);

        auto* chip = new QToolButton(m_legendPanel);
        chip->setText(disp + (isOwn ? QStringLiteral(" (én)") : QString()));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(QStringLiteral("Kattints a hozzárendeléshez / átnevezéshez"));
        // Chip-stílus (téma-követő rgba): saját = kiemelt; ismeretlen = szaggatott/halvány; ismert = kitöltött.
        if (isOwn)
            chip->setStyleSheet(QStringLiteral(
                "QToolButton { background: palette(highlight); color: palette(highlighted-text); "
                "padding: 2px 10px; border-radius: 9px; font-weight: bold; }"));
        else if (unknown)
            chip->setStyleSheet(QStringLiteral(
                "QToolButton { background: transparent; border: 1px dashed palette(mid); "
                "color: palette(text); padding: 2px 10px; border-radius: 9px; }"));
        else
            chip->setStyleSheet(QStringLiteral(
                "QToolButton { background: rgba(127,127,127,0.28); "
                "padding: 2px 10px; border-radius: 9px; }"));
        connect(chip, &QToolButton::clicked, this, [this, raw]() { showAssignBarFor(raw); });
        m_legendLayout->addWidget(chip);
    }

    // „Mindet kezel… (Emberek)" link a globális Személyek-kezelőhöz.
    auto* manage = new QToolButton(m_legendPanel);
    manage->setText(QStringLiteral("Mindet kezel… (Emberek)"));
    manage->setAutoRaise(true);
    manage->setCursor(Qt::PointingHandCursor);
    manage->setStyleSheet(QStringLiteral("QToolButton { color: palette(link); }"));
    connect(manage, &QToolButton::clicked, this, &TranscriptPlayer::managePeopleRequested);

    m_legendLayout->addStretch(1);
    m_legendLayout->addWidget(manage);

    const bool any = !rawSpeakers.isEmpty();
    m_speakersLabel->setVisible(any);
    m_legendPanel->setVisible(any);
}

void TranscriptPlayer::showAssignBarFor(const QString& rawLabel) {
    if (rawLabel.isEmpty())
        return;
    m_assignRaw = rawLabel;
    const QStringList people = m_controller ? m_controller->knownPeople() : QStringList{};
    {
        QSignalBlocker block(m_assignCombo);
        m_assignCombo->clear();
        m_assignCombo->addItems(people);
        m_assignCombo->setCurrentText(displayName(rawLabel));
    }
    m_assignLabel->setText(
        QStringLiteral("🖱 %1 — ki mondta?").arg(displayName(rawLabel)));
    m_assignBar->setVisible(true);
    m_assignCombo->setFocus();
}

void TranscriptPlayer::hideAssignBar() {
    m_assignRaw.clear();
    if (m_assignBar)
        m_assignBar->setVisible(false);
}

void TranscriptPlayer::onTranscriptSelectionChanged() {
    if (!m_view)
        return;
    const QTextCursor c = m_view->textCursor();
    if (!c.hasSelection()) {
        // Üres kijelölés (pl. kattintás-seek) → a sávot becsukjuk.
        hideAssignBar();
        return;
    }
    // A kijelölés KEZDETÉHEZ tartozó blokk = szegmens-index → annak nyers beszélője.
    QTextCursor start = c;
    start.setPosition(c.selectionStart());
    const int blk = start.blockNumber();
    if (blk < 0 || blk >= m_segments.size())
        return;
    const QString raw = m_segments[blk].speaker;
    if (raw.isEmpty())
        return;
    showAssignBarFor(raw);
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

void TranscriptPlayer::onTestSpeaker(const QString& rawLabel) {
    if (!m_controller || m_meetingId.isEmpty())
        return;
    const tanara::VoiceMatch m = m_controller->testSpeakerMatch(m_meetingId, rawLabel);
    if (m.score < 0.0 || m.name.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Fingerprint-teszt"),
            QStringLiteral("Ehhez a beszélőhöz (%1) nincs egyezés a személy-adatbázisban "
                           "(vagy nincs még betanított hang / modell).\n\n"
                           "Adj nevet a sorban — ezzel betanítod a hangját, és onnantól "
                           "automatikusan felismeri.").arg(rawLabel));
        return;
    }
    const int pct = static_cast<int>(m.score * 100.0 + 0.5);
    auto* box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Question);
    box->setWindowTitle(QStringLiteral("Fingerprint-teszt"));
    box->setText(QStringLiteral("„%1” → legjobb egyezés: <b>%2</b> (%3%)")
                     .arg(rawLabel, m.name).arg(pct));
    box->setInformativeText(QStringLiteral("Alkalmazzam ezt a nevet erre a beszélőre?"));
    QPushButton* apply = box->addButton(QStringLiteral("Alkalmaz"), QMessageBox::AcceptRole);
    box->addButton(QStringLiteral("Mégse"), QMessageBox::RejectRole);
    const QString name = m.name;
    connect(box, &QMessageBox::finished, this, [this, box, apply, rawLabel, name](int) {
        if (box->clickedButton() == apply)
            m_controller->renameSpeaker(m_meetingId, rawLabel, name);
    });
    box->open();
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
        m_segmentStarts.clear();
        m_highlightedRow = -1;
        m_view->setExtraSelections({});
        m_view->setPlainText(QStringLiteral("Nincs átirat — futtass Átírást."));
        rebuildLegend();   // nincs szegmens → üres panel, elrejti magát
        setBarEnabled(false);
        m_hasAudio = false;
        m_audioPath.clear();
        return;
    }

    populateList();
    rebuildLegend();

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
    m_segmentStarts.clear();
    m_view->clear();
    m_view->setExtraSelections({});
    rebuildLegend();
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
    // Megkeressük a szegmenst, amelyre: startMs <= pos < (következő.startMs vagy
    // endMs). A startMs-ek időrendben vannak (loadSegments rendezi), így a korábbi
    // lineáris keresés helyett bináris keresés: O(log n)/tick a O(n) helyett.
    // upper_bound → az első olyan szegmens, amelynek startMs-e > pos; a jelölt a
    // megelőző (cand = az utolsó startMs <= pos). A felső határt (upper) ugyanúgy
    // ellenőrizzük, mint a lineáris változat, hogy a kiemelt sor azonos legyen.
    int found = -1;
    const auto it =
        std::upper_bound(m_segmentStarts.cbegin(), m_segmentStarts.cend(), pos);
    const int cand = static_cast<int>(it - m_segmentStarts.cbegin()) - 1;
    if (cand >= 0) {
        const qint64 upper = (cand + 1 < m_segments.size())
                                 ? m_segments[cand + 1].startMs
                                 : m_segments[cand].endMs;
        if (pos < upper)
            found = cand;
    }
    if (found < 0 || found == m_highlightedRow)
        return;
    m_highlightedRow = found;

    // A kiemelt szegmens = a `found`. blokk. FullWidthSelection → a teljes sor
    // halvány sárga, kijelölés (k”kék”) nélkül. ExtraSelection olcsó, nem épít widgetet.
    QTextBlock block = m_view->document()->findBlockByNumber(found);
    if (!block.isValid())
        return;
    QTextCursor cur(block);
    QTextEdit::ExtraSelection sel;
    sel.cursor = cur;                       // kijelölés nélkül, csak pozíció a blokk elején
    sel.format.setBackground(QColor(255, 244, 200));
    // FIX sötét betűszín a sárga háttérhez — különben sötét témán a világos szöveg
    // a sárgán olvashatatlan (klasszikus „kijelölt sor" look, bármelyik témán olvasható).
    sel.format.setForeground(QColor(26, 26, 26));
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    m_view->setExtraSelections({sel});

    // A nézet a kiemelt szegmensre görget (középre), de a felhasználó szöveg-
    // kijelölését nem bántjuk: külön kurzort használunk a görgetéshez.
    QTextCursor scrollCur(block);
    const QTextCursor saved = m_view->textCursor();
    m_view->setTextCursor(scrollCur);
    m_view->centerCursor();
    // A látható (felhasználói) kurzort visszaállítjuk, ha volt kijelölése.
    if (saved.hasSelection())
        m_view->setTextCursor(saved);
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

void TranscriptPlayer::onVolumeChanged(int value) {
    // A hangerő a player létrejötte ELŐTT is állítható (a sáv mindig látszik); ilyenkor
    // az ensurePlayer() majd a csúszka aktuális értékét veszi át. QAudioOutput lineáris.
    if (m_audioOutput)
        m_audioOutput->setVolume(value / 100.0f);
}

void TranscriptPlayer::onSliderMoved(int value) {
    // Húzás közben azonnali seek + címke-frissítés (a feedback ellen m_userSeeking véd).
    if (m_player)
        m_player->setPosition(value);
    updateTimeLabel(value, m_player ? m_player->duration() : 0);
}

void TranscriptPlayer::seekToSegment(int idx) {
    if (idx < 0 || idx >= m_segments.size() || !m_hasAudio)
        return;
    const qint64 startMs = m_segments[idx].startMs;
    const qint64 endMs   = m_segments[idx].endMs;

    ensurePlayer();
    const bool wasPlaying =
        m_player->playbackState() == QMediaPlayer::PlayingState;

    m_player->setPosition(startMs);
    if (wasPlaying) {
        // Folyamatos lejátszás — csak ugrunk, az egy-szegmens módot elhagyjuk.
        m_singleSegmentMode = false;
    } else {
        // Állt → lejátsszuk ezt az egy mondatot, a végén (endMs) megállunk.
        m_singleSegmentMode = endMs > startMs;
        m_singleSegmentEndMs = endMs;
        m_player->play();
    }
}

bool TranscriptPlayer::eventFilter(QObject* obj, QEvent* ev) {
    if (m_view && obj == m_view->viewport()) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton)
                m_pressPos = me->pos();
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(ev);
            // Csak „tiszta” kattintás (nem húzás-kijelölés) ugrik a szegmensre.
            if (me->button() == Qt::LeftButton &&
                (me->pos() - m_pressPos).manhattanLength() < 6 && m_hasAudio) {
                const QTextCursor c = m_view->cursorForPosition(me->pos());
                seekToSegment(c.blockNumber());
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void TranscriptPlayer::playSegmentRange(qint64 startMs, qint64 endMs) {
    if (!m_hasAudio)
        return;
    ensurePlayer();
    // A "Meghallgatás" mindig egy-szegmens lejátszás: a megadott elejére ugrunk,
    // játszunk, és a végén (endMs) automatikusan megállunk.
    m_player->setPosition(startMs);
    m_singleSegmentMode = endMs > startMs;
    m_singleSegmentEndMs = endMs;
    m_player->play();
}

void TranscriptPlayer::playSpeakerSample(const QString& rawSpeaker) {
    if (!m_hasAudio || rawSpeaker.isEmpty())
        return;
    // A beszélő reprezentatív szegmense = a LEGHOSSZABB (max endMs-startMs) az
    // adott beszélő szegmensei közül.
    qint64 bestStart = -1, bestEnd = -1, bestLen = -1;
    for (const Segment& s : m_segments) {
        if (s.speaker != rawSpeaker)
            continue;
        const qint64 len = s.endMs - s.startMs;
        if (len > bestLen) {
            bestLen = len;
            bestStart = s.startMs;
            bestEnd = s.endMs;
        }
    }
    if (bestStart < 0)
        return;   // nincs szegmense ennek a beszélőnek
    playSegmentRange(bestStart, bestEnd);
}

} // namespace tanara_gui
