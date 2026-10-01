#pragma once
//
// „Hamarosan” panel (MKT-02) — a Beállítások → Tanara Cloud szekció állapota a cloud
// indulása ELŐTT (teaser-mód). Indulás után ugyanitt a bejelentkezés (K-02, CloudAccountPanel).
//
// OSS-barát: nincs felugró ablak, indításkori értesítés vagy telemetria; hálózati kérés
// CSAK az „Értesítést kérek” gombra (POST /v1/waitlist). Siker után a feliratkozást a
// settings.json (waitlistEmail) megjegyzi, és a panel nem kérdez újra.
//
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace tanara { class AppController; struct CloudError; }

namespace tanara_gui {

class CloudWaitlistPanel : public QWidget {
    Q_OBJECT
public:
    explicit CloudWaitlistPanel(tanara::AppController* app, QWidget* parent = nullptr);

private:
    void updateButton();
    void submit();
    void showJoined(const QString& email);
    void onFailed(const tanara::CloudError& e);

    tanara::AppController* m_app;
    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_email = nullptr;
    QComboBox* m_useCase = nullptr;
    QCheckBox* m_langHu = nullptr;
    QCheckBox* m_langEn = nullptr;
    QCheckBox* m_langOther = nullptr;
    QCheckBox* m_consent = nullptr;
    QPushButton* m_submit = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_joined = nullptr;
    bool m_busy = false;
};

} // namespace tanara_gui
