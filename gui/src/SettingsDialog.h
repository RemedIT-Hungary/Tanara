#pragma once
//
// SettingsDialog — AppSettings szerkesztése + Soniox API-kulcs beállítása.
//
#include <QDialog>

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;

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
    void onLlmModelsFetched(const QStringList& models);
    void onLlmModelsFailed(const QString& error);

private:
    void loadFromController();

    // Mappa-választó gomb bekötése egy könyvtár-mezőhöz (Tallózás…).
    void wireFolderPicker(QLineEdit* field, QPushButton* button, const QString& caption);

    tanara::AppController* m_controller = nullptr;

    QLineEdit* m_audioDir = nullptr;
    QLineEdit* m_notesDir = nullptr;
    QLineEdit* m_metadataDir = nullptr;
    QLineEdit* m_userSpeakerName = nullptr;
    QLineEdit* m_sttBaseUrl = nullptr;
    QLineEdit* m_sttModel = nullptr;
    QLineEdit* m_llmBaseUrl = nullptr;
    QComboBox* m_llmModel = nullptr;        // szerkeszthető: kézi bevitel is marad
    QPushButton* m_llmFetchBtn = nullptr;
    QLabel* m_llmModelStatus = nullptr;     // inline visszajelzés (hiba)
    QDoubleSpinBox* m_llmTemperature = nullptr; // összefoglaló mintavételezési hőmérséklet
    QSpinBox* m_llmMaxTokens = nullptr;         // összefoglaló válasz max tokenszám
    QLineEdit* m_sonioxApiKey = nullptr;  // jelszó-echo, csak írásra
};

} // namespace tanara_gui
