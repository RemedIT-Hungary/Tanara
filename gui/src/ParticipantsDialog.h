#pragma once
//
// ParticipantsDialog — ÁTÍRÁS ELŐTTI résztvevő-azonosítás. A hang alapján (lokálisan)
// felismeri, kik voltak a meetingen; az ismeretleneket itt elnevezve betanítod, és az
// utána futó átírás+auto-match már nevekkel ad. namespace tanara_gui.
//
#include "tanara/Types.h"
#include <QDialog>
#include <QVector>

class QComboBox;
class QLabel;
class QVBoxLayout;
class QMediaPlayer;
class QAudioOutput;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class ParticipantsDialog : public QDialog {
    Q_OBJECT
public:
    ParticipantsDialog(tanara::AppController* controller, const QString& meetingId,
                       QWidget* parent = nullptr);

private slots:
    void runIdentify();   // a hang-azonosítás (a megnyitás után, hogy a dialógus látszódjon)
    void onSave();        // a megadott neveket betanítja (enroll)

private:
    void playSample(const QString& sampleRef);

    struct Row {
        QString trackId;
        QString sampleRef;   // "track_x.ogg#start-end"
        QString detected;    // a felismert név ("" = ismeretlen)
        QComboBox* combo = nullptr;
    };

    tanara::AppController* m_controller = nullptr;
    QString m_meetingId;
    QString m_folder;

    QLabel*      m_status = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    QVector<Row> m_rows;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    qint64 m_playEndMs = 0;
};

} // namespace tanara_gui
