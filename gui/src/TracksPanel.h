#pragma once
//
// TracksPanel — egy meeting hangsávjainak kezelése: aktív vs (felvétel után) eldobott
// csendes sávok. A user visszaállíthat egy eldobott sávot, vagy VÉGLEG törölheti
// (fizikai fájltörlés). namespace tanara_gui.
//
#include "tanara/Types.h"
#include <QWidget>
#include <QVector>

class QListWidget;
class QPushButton;
class QLabel;
class QMediaPlayer;
class QAudioOutput;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class TracksPanel : public QWidget {
    Q_OBJECT
public:
    explicit TracksPanel(QWidget* parent = nullptr);
    void setController(tanara::AppController* controller);
    void setMeeting(const tanara::Meeting& meeting);
    void clearMeeting();

private slots:
    void onRestoreClicked();
    void onDeleteClicked();
    void onPlayClicked();
    void onStopClicked();
    void updateButtons();

private:
    void populate();

    tanara::AppController* m_controller = nullptr;
    QString m_meetingId;
    QString m_folder;
    QVector<tanara::Track> m_tracks;

    QLabel*      m_hint = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_playBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
};

} // namespace tanara_gui
