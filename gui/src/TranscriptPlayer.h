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
#include "tanara/Types.h"

#include <QWidget>
#include <QString>
#include <QVector>
#include <QMap>

class QPushButton;
class QSlider;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMediaPlayer;
class QAudioOutput;
class QComboBox;
class QHBoxLayout;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class TranscriptPlayer : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptPlayer(QWidget* parent = nullptr);
    ~TranscriptPlayer() override;

    // A controllert a beszélő-átnevezéshez (renameSpeaker) + autocomplete-hez
    // (knownPeople) használjuk. Beállítható konstrukció után is.
    void setController(tanara::AppController* controller);

    // Egy meeting betöltése: szegmensek + hangforrás + beszélő-leképezés.
    //   A nyers címkék helyett a speakerMap szerinti valódi neveket jelenítjük meg
    //   (ha van leképezés), de a nyers címkét megőrizzük az átnevezéshez.
    //   audioPath:        <folder>/<mixdownFile>  (üres/nemlétező → sáv tiltva)
    // A lejátszás NEM indul automatikusan.
    void loadMeeting(const tanara::Meeting& meeting, const QString& audioPath);

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
        QString speaker;    // NYERS beszélő-címke (a segments.json-ból)
        QString text;
    };

    void ensurePlayer();                 // lusta QMediaPlayer-létrehozás
    void setBarEnabled(bool on);
    void highlightForPosition(qint64 pos);
    bool loadSegments(const QString& path);   // segments.json → m_segments
    void populateList();
    void populateSpeakersPanel();        // a "Beszélők" sor (combo-k) felépítése
    QString displayName(const QString& rawSpeaker) const;  // speakerMap szerinti név
    void onSpeakerRenamed(const QString& rawLabel, const QString& chosenName);
    static QString formatTime(qint64 ms);
    void updateTimeLabel(qint64 pos, qint64 dur);

    tanara::AppController* m_controller = nullptr;
    QString m_meetingId;
    QMap<QString, QString> m_speakerMap;   // nyers címke → valódi név (a meetingből)

    QLabel*      m_speakersLabel = nullptr;
    QWidget*     m_speakersPanel = nullptr;
    QHBoxLayout* m_speakersLayout = nullptr;

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
