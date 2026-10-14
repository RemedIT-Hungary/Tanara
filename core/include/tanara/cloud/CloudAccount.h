#pragma once
//
// CloudAccount: the client-side state of the Tanara Cloud account + the gateway's "own" (not
// Soniox/OpenAI-shaped) endpoints: device flow, logout, account, models, estimate,
// terms acceptance, topup link, waitlist, and the write-back check of interrupted transcriptions.
//
// STT/LLM processing does NOT run here: that is the existing SonioxProvider /
// OpenAiCompatibleProvider with the gateway config (see AppController). They report every HTTP
// response (HttpExchange) back here too (observeExchange), so min-client and the balance update in one place.
//
// State: the API key in the SecretStore (tanara.cloud.apiKey), the rest in
// <data>/cloud-state.json (email, last known account/catalog, pending transcriptions,
// terms "Later", hidden tracks). Headless (core): no Widgets dependency.
#include "tanara/cloud/CloudTypes.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace tanara { class SecretStore; }

namespace tanara::cloud {


class CloudAccount : public QObject {
    Q_OBJECT
public:
    // keys: the caller's (AppController), its lifetime >= this one's. dataDir: AppPaths::dataDir() (cloud-state.json).
    CloudAccount(SecretStore* keys, const QString& dataDir, QObject* parent = nullptr);
    ~CloudAccount() override;

    // Gateway base URL (WITHOUT /v1), e.g. https://api.tanara.remedit.hu or http://127.0.0.1:8300.
    void    setBaseUrl(const QString& url);
    QString baseUrl() const { return m_baseUrl; }
    QString apiBase() const { return m_baseUrl + QStringLiteral("/v1"); }
    void    setLanguage(const QString& lang) { m_lang = lang; }   // Accept-Language: hu | en
    QString language() const { return m_lang; }

    bool    isLoggedIn() const;
    QString apiKey() const;
    QString email() const { return m_email; }

    // Last known account state (also offline, from cloud-state.json).
    AccountInfo account() const { return m_account; }
    QDateTime   accountFetchedAt() const { return m_accountAt; }
    QVector<CloudModel> models() const { return m_models; }

    // Min-client: if any response's X-Tanara-Min-Client is > our version, or a 426 came.
    bool    clientTooOld() const { return m_tooOld; }
    QString minClient() const { return m_minClient; }
    QString downloadUrl() const { return m_downloadUrl; }

    // Common headers of gateway calls (WITHOUT Authorization, which the provider adds).
    // An empty jobId / summaryMode omits the header.
    QList<QPair<QByteArray, QByteArray>> requestHeaders(const QString& jobId = QString(),
                                                         const QString& summaryMode = QString()) const;

    // Common handling of a gateway response: min-client, balance from the charge headers.
    void observeExchange(const HttpExchange& ex);

    // ---- pending transcriptions (write-back check at startup) ----
    void addPendingTranscription(const QString& transcriptionId, const QString& fileId);
    void removePendingTranscription(const QString& transcriptionId);
    QStringList pendingTranscriptions() const { return m_pending.keys(); }

    // ---- local UI state (in cloud-state.json) ----
    QDateTime termsPostponedAt() const { return m_termsPostponed; }
    void      postponeTerms();                         // "Later" (at most once a day)
    QDateTime lowBannerDismissedAt() const { return m_lowDismissed; }
    void      dismissLowBanner();
    bool      isNoticeDismissed(const QString& id) const { return m_dismissedNotices.contains(id); }
    void      dismissNotice(const QString& id);

    // ---- calls (asynchronous; results in the signals) ----
    void startDeviceFlow();
    void cancelDeviceFlow();
    void logout();                                     // the local key is deleted even on error
    void refreshAccount();
    void fetchModels();
    void estimate(const EstimateRequest& req,
                  std::function<void(const EstimateResult&, const CloudError&)> done);
    void acceptTerms(const QString& version);
    void createTopupLink();
    void joinWaitlist(const WaitlistSignup& signup);   // without auth (teaser mode too)
    void checkPendingTranscriptions();

    // Manual key (from the dashboard): CLI / test.
    void setManualApiKey(const QString& key, const QString& email = QString());

signals:
    void deviceCodeReady(const tanara::cloud::DeviceCode& code);
    void deviceFlowSucceeded(const QString& email);
    void deviceFlowFailed(const tanara::cloud::CloudError& error);   // access_denied / expired_token / network ...
    void loggedIn();
    void loggedOut();
    void accountUpdated(const tanara::cloud::AccountInfo& account);
    void accountFailed(const tanara::cloud::CloudError& error);
    void modelsUpdated();
    void modelsFailed(const tanara::cloud::CloudError& error);
    void balanceChanged(const tanara::cloud::Money& balance);
    void termsAccepted(const tanara::cloud::TermsStatus& terms);
    void termsFailed(const tanara::cloud::CloudError& error);
    void topupLinkReady(const QString& url);
    void topupFailed(const tanara::cloud::CloudError& error);   // TopupUnavailable → contactUrl
    void waitlistJoined(const QString& email);
    void waitlistFailed(const tanara::cloud::CloudError& error);
    void clientTooOldDetected(const QString& minClient);
    // Startup check: an earlier (interrupted) transcription ended with an error and the charge was written back.
    void previousTranscriptionRefunded(const tanara::cloud::Money& refund, const tanara::cloud::Money& balance);

private:
    using Done = std::function<void(const HttpExchange&)>;
    void send(const QByteArray& method, const QString& path, const QJsonObject* body,
              bool authenticated, Done done);
    void pollDeviceToken();
    void finishDeviceFlow();
    void loadState();
    void saveState() const;

    SecretStore* m_keys = nullptr;
    QString     m_statePath;
    QString     m_baseUrl;
    QString     m_lang = QStringLiteral("hu");
    QNetworkAccessManager* m_nam = nullptr;

    QString     m_email;
    AccountInfo m_account;
    QDateTime   m_accountAt;
    QVector<CloudModel> m_models;
    QJsonObject m_modelsRaw;       // raw form of the catalog (for persistence)
    QJsonObject m_accountRaw;

    bool    m_tooOld = false;
    QString m_minClient, m_downloadUrl;

    QHash<QString, QString> m_pending;   // transcriptionId → fileId
    QDateTime   m_termsPostponed, m_lowDismissed;
    QStringList m_dismissedNotices;

    // device flow
    DeviceCode m_device;
    QTimer*    m_pollTimer = nullptr;
    QDateTime  m_deviceExpires;
    bool       m_deviceActive = false;
};

} // namespace tanara::cloud