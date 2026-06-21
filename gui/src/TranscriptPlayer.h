#pragma once
//
// TranscriptPlayer — szinkronizált lejátszó + kattintható átirat.
//   felül:  lejátszó-sáv (Play/Pause + seek-csúszka + mm:ss / mm:ss címke)
//   alatta: szegmens-nézet (QPlainTextEdit — szegmensenként egy blokk), kattintásra
//           a lejátszó az adott szegmens elejére ugrik; a lejátszott szegmens kiemelve.
//           A QPlainTextEdit dokumentum-motorja csak a látható blokkokat rendereli,
//           így több ezer szegmensnél (hosszú meeting) is sima marad (a korábbi
//           word-wrap-es QListWidget az átméretezéskor minden itemet újraszámolt).
//
// Csak gui/-ben él, namespace tanara_gui. A QMediaPlayer LUSTÁN jön létre
// (az első forrás-betöltéskor), hogy az app indítása tiszta maradjon.
//
#include "tanara/Types.h"

#include <QWidget>
#include <QString>
#include <QVector>
#include <QMap>
#include <QPoint>
#include <QColor>

class QPushButton;
class QSlider;
class QLabel;
class QPlainTextEdit;
class QMediaPlayer;
class QAudioOutput;
class QHBoxLayout;
class QVBoxLayout;
class QFrame;
class QSyntaxHighlighter;

namespace tanara {
class AppController;
}

namespace tanara_gui {

// Egy átirat-sor kattintható tartományai (oszlop-pozíciók a blokkon belül), hogy a
// QPlainTextEdit-en rich-text/anchor nélkül is „hiperlink-szerűen" működjenek az
// időbélyeg és a név CTA-k (a kattintás oszlopát ehhez mérjük, és a link-stílust is
// ezekre a tartományokra rajzolja a QSyntaxHighlighter).
struct LineMeta {
    int tsLen = 0;        // a „[mm:ss]" hossza → [0, tsLen) = időbélyeg-CTA
    int nameStart = -1;   // a név kezdő oszlopa (-1, ha nincs név)
    int nameLen = 0;      // a név hossza → [nameStart, nameStart+nameLen) = név-CTA
    QColor nameColor;     // beszélőnként eltérő szín (a név félkövéren ezzel jelenik meg)
};

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

    // A lejátszó-sáv (Play/Pause + seek + idő + hangerő) KÜLÖN beágyazható widgetje.
    // A MainWindow kiemeli a fülekből és a jobb pane aljára, MINDIG látható helyre teszi
    // (Könyvtár-otthon mockup). A TranscriptPlayer megtartja a tulajdonjogot/logikát;
    // ez csak a vizuális elhelyezést adja át. A reparenting után is a TranscriptPlayer
    // kezeli a seeket/kiemelést/odaugrást.
    QWidget* playerBar() const { return m_playerBar; }

protected:
    // A szegmens-nézet (QPlainTextEdit viewport) kattintásait figyeljük: a kattintott
    // blokk = szegmens-index → odaugrás. (A húzás-szöveges kijelölést nem zavarjuk.)
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onPlayPauseClicked();
    void onDurationChanged(qint64 dur);
    void onPositionChanged(qint64 pos);
    void onPlaybackStateChanged();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderMoved(int value);
    void onVolumeChanged(int value);

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
    // A lejátszás-kiemelést (aktuális szegmens) és a hover-token kiemelést EGY helyen
    // fésüli össze és teszi ki a nézetre (mindkettő ExtraSelection, olcsó).
    void updateExtraSelections();
    void seekToSegment(int idx);         // egy szegmensre ugrás (kattintás-logika)
    bool loadSegments(const QString& path);   // segments.json → m_segments
    void populateList();                 // m_segments → a QPlainTextEdit blokkjai
    void rebuildLegend();                // kompakt beszélő-legenda (chipek) felépítése
    // A névre kattintva előugró menü: hozzárendelés meglévő névhez / új név (= azonnal
    // fingerprint a személy-DB-be) / meghallgatás / név törlése.
    void showSpeakerMenu(const QString& rawLabel, const QPoint& globalPos);
    QString displayName(const QString& rawSpeaker) const;  // speakerMap szerinti név
    void onSpeakerRenamed(const QString& rawLabel, const QString& chosenName);
    // Egy beszélő "reprezentatív" (leghosszabb) szegmensét lejátssza (azonosításhoz).
    void playSpeakerSample(const QString& rawSpeaker);
    void playSegmentRange(qint64 startMs, qint64 endMs);   // egy-szegmens lejátszás
    static QString formatTime(qint64 ms);
    void updateTimeLabel(qint64 pos, qint64 dur);

    tanara::AppController* m_controller = nullptr;
    QString m_meetingId;
    QMap<QString, QString> m_speakerMap;   // nyers címke → valódi név (a meetingből)

    // Kompakt legenda-chipek (ki van a meetingen) — áttekintés; a névadás a chipre VAGY
    // az átiratban a névre kattintva előugró menüvel történik.
    QLabel*      m_speakersLabel = nullptr;
    QWidget*     m_legendPanel = nullptr;
    QHBoxLayout* m_legendLayout = nullptr;     // chipek vízszintesen

    QWidget*        m_playerBar = nullptr;   // a beágyazható lejátszó-sáv (kiemelhető)
    QPushButton*    m_playPauseBtn = nullptr;
    QSlider*        m_seekSlider = nullptr;
    QLabel*         m_timeLabel = nullptr;
    QSlider*        m_volumeSlider = nullptr;
    QPlainTextEdit* m_view = nullptr;     // szegmensenként egy blokk
    QSyntaxHighlighter* m_highlighter = nullptr;  // időbélyeg/név link-stílusa
    QVector<LineMeta>   m_lineMeta;       // soronkénti kattintható tartományok
    QPoint          m_pressPos;           // kattintás kezdő-pozíció (click vs. drag)
    // A hover alatti link-token (block + oszlop-tartomány) a finom háttér-kiemeléshez.
    int m_hoverBlock = -1;
    int m_hoverStart = -1;
    int m_hoverLen = 0;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    QVector<Segment> m_segments;
    // A szegmensek startMs-ei időrendben, a highlightForPosition() bináris
    // kereséséhez. Egyszer épül fel (loadSegments), így a kiemelés O(log n)/tick.
    QVector<qint64>  m_segmentStarts;
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
