#pragma once
//
// SettingsDialog — AppSettings szerkesztése generikus, descriptor-vezérelt
// rendererrel. A provider-mezők a kiválasztott ProviderDescriptor.fields-éből
// épülnek; a titkok a KeyStore-ba mennek (nem a settings.json-be).
//
#include <QDialog>
#include <QHash>
#include <QString>

#include "tanara/provider/ProviderDescriptor.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QWidget;
class QFormLayout;
class QVBoxLayout;
class QGroupBox;
class QCheckBox;
class QPlainTextEdit;
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
    void loadGeneral();

    // A Rögzítés fül eszköz-policy listája (capture-eszközök checkboxai, Mic/Loopback/
    // Egyéb csoportban) — a megadott layoutba építve. A választás a lastUsedDeviceNames-be
    // megy (ugyanaz a default-halmaz, amit a felvevő használ).
    void buildDevicePolicy(QVBoxLayout* into);

    // Mappa-választó gomb bekötése egy könyvtár-mezőhöz (Tallózás…).
    void wireFolderPicker(QLineEdit* field, QPushButton* button, const QString& caption);

    // Egy provider-blokk (combo + dinamikus mező-form) belső állapota.
    struct ProviderSection {
        tanara::ProviderKind kind = tanara::ProviderKind::Stt;
        QComboBox* selector = nullptr;       // provider-választó (id a userData-ban)
        QFormLayout* fieldsForm = nullptr;   // ide épülnek a mezők
        QString currentId;                   // épp megjelenített descriptor id-ja
        QHash<QString, QWidget*> widgets;    // ConfigField.key -> szerkesztő widget
        QComboBox* dynamicCombo = nullptr;   // a dynamicOptions-os combo (modell-lekéréshez)
        QLabel* statusLabel = nullptr;       // inline visszajelzés (modell-lekérés hibája)
    };

    // A kiválasztott provider descriptor.fields-éből újraépíti a mező-formot.
    void rebuildFields(ProviderSection& section, const QString& providerId);

    // Egy ConfigField-hez illő szerkesztő widget. dynamicCombo/statusLabel
    // töltése a section-ön keresztül (a "Modellek lekérése" gomb bekötéséhez).
    QWidget* makeWidgetFor(ProviderSection& section, const tanara::ConfigField& field,
                           QWidget* parent);

    // A section aktuális (kiválasztott) configját kiolvassa a widgetekből és a
    // megfelelő config-mapba írja; a nem-üres titkokat a KeyStore-ba menti.
    void collectSection(ProviderSection& section);

    tanara::AppController* m_controller = nullptr;

    // --- Általános mezők ---
    QLineEdit* m_audioDir = nullptr;
    QLineEdit* m_notesDir = nullptr;
    QLineEdit* m_metadataDir = nullptr;
    QLineEdit* m_userSpeakerName = nullptr;
    QComboBox* m_uiLanguage = nullptr;     // UI-nyelv, userData = "auto"|"hu"|"en"
    QCheckBox* m_autoRecord = nullptr;
    QComboBox* m_audioQuality = nullptr;   // hangminőség (per-sáv Opus bitráta), userData = id
    QComboBox* m_mixdownMode = nullptr;    // lekeverés időzítése, userData = "auto"|"manual"
    QLabel*    m_qualityHint = nullptr;    // a választott fokozat méret-becslése
    // Összefoglaló-promptok: egy szerkesztő + választó (Egyszerű / Téma-kinyerés / Téma-elemzés).
    QComboBox*      m_promptSelect = nullptr;     // melyik promptot szerkesztjük (userData=id)
    QComboBox*      m_summaryLanguage = nullptr;  // összefoglaló célnyelve (szabad szöveg)
    QPlainTextEdit* m_summaryPrompt = nullptr;    // a kiválasztott prompt szerkesztője
    QHash<QString, QString> m_promptText;         // id → aktuális szöveg (váltáskor megőrizve)
    QString        m_curPromptId;                 // épp szerkesztett prompt id-ja

    // Rögzítés fül — eszköz-policy.
    QWidget* m_devicesGroup = nullptr;
    QHash<QString, QCheckBox*> m_deviceChecks;   // eszköznév → checkbox

    // Figyelő fül — háttér-detektor (aktív hívás észlelése) + tray.
    QCheckBox*      m_detectorEnabled = nullptr;
    QSpinBox*       m_detectorInterval = nullptr;   // poll-intervallum (mp)
    QCheckBox*      m_watcherAutostart = nullptr;
    QCheckBox*      m_askStopOnCallEnd = nullptr;   // felvétel közben kérdezzen a hívás végén
    QPlainTextEdit* m_knownCallApps = nullptr;      // soronként egy app (bináris/név-részlet)

    ProviderSection m_stt;
    ProviderSection m_llm;
};

} // namespace tanara_gui
