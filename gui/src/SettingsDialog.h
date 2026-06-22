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
    QCheckBox* m_autoRecord = nullptr;

    // Rögzítés fül — eszköz-policy.
    QWidget* m_devicesGroup = nullptr;
    QHash<QString, QCheckBox*> m_deviceChecks;   // eszköznév → checkbox

    ProviderSection m_stt;
    ProviderSection m_llm;
};

} // namespace tanara_gui
