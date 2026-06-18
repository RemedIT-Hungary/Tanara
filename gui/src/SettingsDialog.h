#pragma once
//
// SettingsDialog — AppSettings szerkesztése + Soniox API-kulcs beállítása.
//
#include <QDialog>

class QLineEdit;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(tanara::AppController* controller, QWidget* parent = nullptr);

private slots:
    void onAccept();

private:
    void loadFromController();

    tanara::AppController* m_controller = nullptr;

    QLineEdit* m_audioDir = nullptr;
    QLineEdit* m_notesDir = nullptr;
    QLineEdit* m_userSpeakerName = nullptr;
    QLineEdit* m_sttBaseUrl = nullptr;
    QLineEdit* m_sttModel = nullptr;
    QLineEdit* m_llmBaseUrl = nullptr;
    QLineEdit* m_llmModel = nullptr;
    QLineEdit* m_sonioxApiKey = nullptr;  // jelszó-echo, csak írásra
};

} // namespace tanara_gui
