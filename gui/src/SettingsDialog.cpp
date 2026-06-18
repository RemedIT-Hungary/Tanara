#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/Types.h"

#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>

namespace tanara_gui {

SettingsDialog::SettingsDialog(tanara::AppController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Beállítások"));
    setModal(true);

    auto* root = new QVBoxLayout(this);

    // --- mappák ---
    auto* dirsBox = new QGroupBox(QStringLiteral("Mappák"), this);
    auto* dirsForm = new QFormLayout(dirsBox);
    m_audioDir = new QLineEdit(dirsBox);
    m_notesDir = new QLineEdit(dirsBox);
    m_userSpeakerName = new QLineEdit(dirsBox);
    dirsForm->addRow(QStringLiteral("Felvételek mappája:"), m_audioDir);
    dirsForm->addRow(QStringLiteral("Jegyzetek mappája:"), m_notesDir);
    dirsForm->addRow(QStringLiteral("Saját beszélő neve:"), m_userSpeakerName);
    root->addWidget(dirsBox);

    // --- STT (Soniox) ---
    auto* sttBox = new QGroupBox(QStringLiteral("Átírás (Soniox)"), this);
    auto* sttForm = new QFormLayout(sttBox);
    m_sttBaseUrl = new QLineEdit(sttBox);
    m_sttModel = new QLineEdit(sttBox);
    m_sonioxApiKey = new QLineEdit(sttBox);
    m_sonioxApiKey->setEchoMode(QLineEdit::Password);
    m_sonioxApiKey->setPlaceholderText(QStringLiteral("(változatlan, ha üresen hagyod)"));
    sttForm->addRow(QStringLiteral("Alap URL:"), m_sttBaseUrl);
    sttForm->addRow(QStringLiteral("Modell:"), m_sttModel);
    sttForm->addRow(QStringLiteral("API-kulcs:"), m_sonioxApiKey);
    root->addWidget(sttBox);

    // --- LLM ---
    auto* llmBox = new QGroupBox(QStringLiteral("Összefoglaló (LLM)"), this);
    auto* llmForm = new QFormLayout(llmBox);
    m_llmBaseUrl = new QLineEdit(llmBox);
    m_llmModel = new QLineEdit(llmBox);
    llmForm->addRow(QStringLiteral("Alap URL:"), m_llmBaseUrl);
    llmForm->addRow(QStringLiteral("Modell:"), m_llmModel);
    root->addWidget(llmBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadFromController();
}

void SettingsDialog::loadFromController() {
    if (!m_controller || !m_controller->settings())
        return;
    const tanara::AppSettings s = m_controller->settings()->settings();
    m_audioDir->setText(s.audioDir);
    m_notesDir->setText(s.notesDir);
    m_userSpeakerName->setText(s.userSpeakerName);
    m_sttBaseUrl->setText(s.stt.baseUrl);
    m_sttModel->setText(s.stt.model);
    m_llmBaseUrl->setText(s.llm.baseUrl);
    m_llmModel->setText(s.llm.model);
    // Az API-kulcsot SOHA nem töltjük vissza (titok).
}

void SettingsDialog::onAccept() {
    if (!m_controller || !m_controller->settings()) {
        accept();
        return;
    }

    tanara::AppSettings s = m_controller->settings()->settings();
    s.audioDir = m_audioDir->text().trimmed();
    s.notesDir = m_notesDir->text().trimmed();
    s.userSpeakerName = m_userSpeakerName->text().trimmed();
    s.stt.baseUrl = m_sttBaseUrl->text().trimmed();
    s.stt.model = m_sttModel->text().trimmed();
    s.llm.baseUrl = m_llmBaseUrl->text().trimmed();
    s.llm.model = m_llmModel->text().trimmed();

    m_controller->settings()->setSettings(s);

    const QString key = m_sonioxApiKey->text();
    if (!key.isEmpty())
        m_controller->setSecret(tanara::keys::SonioxApiKey, key);

    accept();
}

} // namespace tanara_gui
