#pragma once
//
// TranscriptPlayer — szinkronizált lejátszó + kattintható átirat.
//   felül:  lejátszó-sáv (Play/Pause + seek-csúszka + mm:ss / mm:ss címke)
//   alatta: szegmens-lista (QListWidget), kattintásra a lejátszó az adott
//           szegmens elejére ugrik; a lejátszott szegmens kiemelve.
//
// Csak gui/-ben él, namespace tanara_gui. A QMediaPlayer LUSTÁN jön létre
// (az első forrás-betöltéskor), hogy az app indítása tiszta maradjon.
//
#include <QWidget>
#include <QString>
#include <QVector>

class QPushButton;
class QSlider;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMediaPlayer;
class QAudioOutput;

namespace tanara_gui {

class TranscriptPlayer : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptPlayer(QWidget* parent = nullptr);
    ~TranscriptPlayer() override;

    // Egy meeting betöltése: szegmensek + hangforrás.
    //   segmentsJsonPath: <folder>/transcript.segments.json
    //   audioPath:        <folder>/<mixdownFile>  (üres/nemlétező → sáv tiltva)
    // A lejátszás NEM indul automatikusan.
    void loadMeeting(const QString& segmentsJsonPath, const QString& audioPath);

    // Üres állapot (nincs kijelölt meeting).
    void clearMeeting();

private slots:
    void onPlayPauseClicked();
    void onDurationChanged(qint64 dur);
    void onPositionChanged(qint64 pos);
    void onPlaybackStateChanged();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderMoved(int value);
    void onItemClicked(QListWidgetItem* item);

private:
    struct Segment {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString speaker;
        QString text;
    };

    void ensurePlayer();                 // lusta QMediaPlayer-létrehozás
    void setBarEnabled(bool on);
    void highlightForPosition(qint64 pos);
    bool loadSegments(const QString& path);   // segments.json → m_segments
    void populateList();
    static QString formatTime(qint64 ms);
    void updateTimeLabel(qint64 pos, qint64 dur);

    QPushButton* m_playPauseBtn = nullptr;
    QSlider*     m_seekSlider = nullptr;
    QLabel*      m_timeLabel = nullptr;
    QListWidget* m_list = nullptr;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    QVector<Segment> m_segments;
    QString m_audioPath;
    bool m_hasAudio = false;

    bool   m_userSeeking = false;     // a felhasználó épp húzza a csúszkát
    int    m_highlightedRow = -1;     // jelenleg kiemelt szegmens

    // "egy-szegmens lejátszás" mód: kattintásra, ha állt a lejátszó,
    // a szegmens végén (endMs) megáll.
    bool   m_singleSegmentMode = false;
    qint64 m_singleSegmentEndMs = 0;
};

} // namespace tanara_gui
