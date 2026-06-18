#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/Types.h"

#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QSizePolicy>
#include <QDir>

namespace tanara_gui {

SettingsDialog::SettingsDialog(tanara::AppController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Beállítások"));
    setModal(true);

    auto* root = new QVBoxLayout(this);

    // Segéd: könyvtár-mező + "Tallózás…" gomb egy sorba.
    auto makeDirRow = [this](QWidget* parentWidget, QLineEdit*& field,
                             const QString& caption) -> QWidget* {
        auto* container = new QWidget(parentWidget);
        auto* h = new QHBoxLayout(container);
        h->setContentsMargins(0, 0, 0, 0);
        field = new QLineEdit(container);
        auto* browse = new QPushButton(QStringLiteral("Tallózás…"), container);
        h->addWidget(field, 1);
        h->addWidget(browse, 0);
        wireFolderPicker(field, browse, caption);
        return container;
    };

    // --- mappák ---
    auto* dirsBox = new QGroupBox(QStringLiteral("Mappák"), this);
    auto* dirsForm = new QFormLayout(dirsBox);
    dirsForm->addRow(QStringLiteral("Felvételek mappája:"),
                     makeDirRow(dirsBox, m_audioDir, QStringLiteral("Felvételek mappája")));
    dirsForm->addRow(QStringLiteral("Jegyzetek mappája:"),
                     makeDirRow(dirsBox, m_notesDir, QStringLiteral("Jegyzetek mappája")));
    dirsForm->addRow(QStringLiteral("Metaadat mappája:"),
                     makeDirRow(dirsBox, m_metadataDir, QStringLiteral("Metaadat mappája")));
    m_userSpeakerName = new QLineEdit(dirsBox);
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

    // Modell: szerkeszthető combo (kézi bevitel marad) + "Modellek lekérése" gomb.
    auto* modelRow = new QWidget(llmBox);
    auto* modelH = new QHBoxLayout(modelRow);
    modelH->setContentsMargins(0, 0, 0, 0);
    m_llmModel = new QComboBox(modelRow);
    m_llmModel->setEditable(true);
    m_llmModel->setInsertPolicy(QComboBox::NoInsert);
    m_llmModel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_llmFetchBtn = new QPushButton(QStringLiteral("Modellek lekérése"), modelRow);
    modelH->addWidget(m_llmModel, 1);
    modelH->addWidget(m_llmFetchBtn, 0);

    m_llmModelStatus = new QLabel(llmBox);
    m_llmModelStatus->setWordWrap(true);
    m_llmModelStatus->setStyleSheet(QStringLiteral("color: #b00020;"));
    m_llmModelStatus->setVisible(false);

    llmForm->addRow(QStringLiteral("Alap URL:"), m_llmBaseUrl);
    llmForm->addRow(QStringLiteral("Modell:"), modelRow);
    llmForm->addRow(QString(), m_llmModelStatus);
    root->addWidget(llmBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // LLM-modellek lekérése a gombbal; az AppController végzi a HTTP-hívást.
    if (m_controller) {
        connect(m_llmFetchBtn, &QPushButton::clicked, this, [this]() {
            m_llmModelStatus->setVisible(false);
            m_llmFetchBtn->setEnabled(false);
            m_llmFetchBtn->setText(QStringLiteral("Lekérés…"));
            m_controller->fetchLlmModels();
        });
        connect(m_controller, &tanara::AppController::llmModelsFetched,
                this, &SettingsDialog::onLlmModelsFetched);
        connect(m_controller, &tanara::AppController::llmModelsFailed,
                this, &SettingsDialog::onLlmModelsFailed);
    } else {
        m_llmFetchBtn->setEnabled(false);
    }

    loadFromController();
}

void SettingsDialog::wireFolderPicker(QLineEdit* field, QPushButton* button,
                                      const QString& caption) {
    connect(button, &QPushButton::clicked, this, [this, field, caption]() {
        QString start = field->text().trimmed();
        if (start.isEmpty())
            start = QDir::homePath();
        const QString dir = QFileDialog::getExistingDirectory(this, caption, start);
        if (!dir.isEmpty())
            field->setText(dir);
    });
}

void SettingsDialog::onLlmModelsFetched(const QStringList& models) {
    m_llmFetchBtn->setEnabled(true);
    m_llmFetchBtn->setText(QStringLiteral("Modellek lekérése"));
    m_llmModelStatus->setVisible(false);

    // A jelenlegi (kézzel beírt/kiválasztott) értéket őrizzük meg, ha még szerepel.
    const QString current = m_llmModel->currentText();
    m_llmModel->clear();
    m_llmModel->addItems(models);

    const int idx = m_llmModel->findText(current);
    if (idx >= 0)
        m_llmModel->setCurrentIndex(idx);
    else
        m_llmModel->setEditText(current);  // megtartjuk a kézi értéket akkor is, ha nincs a listában
}

void SettingsDialog::onLlmModelsFailed(const QString& error) {
    m_llmFetchBtn->setEnabled(true);
    m_llmFetchBtn->setText(QStringLiteral("Modellek lekérése"));
    m_llmModelStatus->setText(QStringLiteral("Nem sikerült lekérni a modelleket: %1").arg(error));
    m_llmModelStatus->setVisible(true);
}

void SettingsDialog::loadFromController() {
    if (!m_controller || !m_controller->settings())
        return;
    const tanara::AppSettings s = m_controller->settings()->settings();
    m_audioDir->setText(s.audioDir);
    m_notesDir->setText(s.notesDir);
    m_metadataDir->setText(s.metadataDir);
    m_userSpeakerName->setText(s.userSpeakerName);
    m_sttBaseUrl->setText(s.stt.baseUrl);
    m_sttModel->setText(s.stt.model);
    m_llmBaseUrl->setText(s.llm.baseUrl);
    m_llmModel->setEditText(s.llm.model);
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
    s.metadataDir = m_metadataDir->text().trimmed();
    s.userSpeakerName = m_userSpeakerName->text().trimmed();
    s.stt.baseUrl = m_sttBaseUrl->text().trimmed();
    s.stt.model = m_sttModel->text().trimmed();
    s.llm.baseUrl = m_llmBaseUrl->text().trimmed();
    s.llm.model = m_llmModel->currentText().trimmed();

    m_controller->settings()->setSettings(s);

    const QString key = m_sonioxApiKey->text();
    if (!key.isEmpty())
        m_controller->setSecret(tanara::keys::SonioxApiKey, key);

    accept();
}

} // namespace tanara_gui
