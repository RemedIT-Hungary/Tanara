#pragma once
// Sign-in with a device code (device flow). The client shows a code, opens the browser
// (verification_uri_complete) and polls every `interval` seconds. Cancel calls
// POST /v1/auth/device/cancel (no orphan key is left).
#include "tanara/cloud/CloudTypes.h"

#include <QDateTime>
#include <QDialog>

class QLabel;
class QPushButton;
class QTimer;
class QWidget;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit CloudLoginDialog(tanara::AppController* app, QWidget* parent = nullptr);

public slots:
    void reject() override;          // Cancel calls device/cancel

protected:
    void showEvent(QShowEvent* e) override;

private:
    void start();
    void onCode(const tanara::DeviceCode& code);
    void setState(const QString& glyph, const QString& text, const QString& retryLabel);
    void tick();

    tanara::AppController* m_app;
    tanara::DeviceCode m_code;
    QDateTime m_expires;
    bool m_started = false;
    bool m_done = false;

    QLabel* m_intro = nullptr;
    QLabel* m_codeLabel = nullptr;
    QLabel* m_wait = nullptr;
    QLabel* m_state = nullptr;
    QWidget* m_fallback = nullptr;
    QLabel* m_fallbackUri = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_reopen = nullptr;
    QPushButton* m_retry = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace tanara_gui
