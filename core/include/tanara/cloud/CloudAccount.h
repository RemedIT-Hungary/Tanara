#pragma once
//
// CloudAccount — a Tanara Cloud fiók kliensoldali állapota + a gateway „saját” (nem
// Soniox/OpenAI-alakú) végpontjai: device flow, logout, account, models, estimate,
// terms-acceptance, topup-link, waitlist, és a félbemaradt átírások visszaírás-ellenőrzése.
//
// Az STT/LLM feldolgozás NEM itt fut: az a meglévő SonioxProvider / OpenAiCompatibleProvider
// a gateway-configgal (lásd AppController). Ezek minden HTTP-válaszukat (HttpExchange) ide
// is visszajelzik (observeExchange), így a min-client és az egyenleg egy helyen frissül.
//
// Állapot: API-kulcs a KeyStore-ban (tanara.cloud.apiKey), a többi a
// <metadataDir>/cloud-state.json-ban (e-mail, utolsó ismert fiók/katalógus, függő átírások,
// ÁSZF-„Később”, elrejtett sávok). Headless (core): nincs Widgets-függőség.
//
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

namespace tanara {

class KeyStore;

class CloudAccount : public QObject {
    Q_OBJECT
public:
    // keys: a hívóé (AppController), élettartama ≥ ennek. stateDir: ~/.tanara.
    CloudAccount(KeyStore* keys, const QString& stateDir, QObject* parent = nullptr);
    ~CloudAccount() override;

    // Gateway alap-URL (/v1 NÉLKÜL), pl. https://api.tanara.remedit.hu vagy http://127.0.0.1:8300.
    void    setBaseUrl(const QString& url);
    QString baseUrl() const { return m_baseUrl; }
    QString apiBase() const { return m_baseUrl + QStringLiteral("/v1"); }
    void    setLanguage(const QString& lang) { m_lang = lang; }   // Accept-Language: hu | en
    QString language() const { return m_lang; }

    bool    isLoggedIn() const;
    QString apiKey() const;
    QString email() const { return m_email; }

    // Utolsó ismert fiók-állapot (offline is — a cloud-state.json-ból).
    AccountInfo account() const { return m_account; }
    QDateTime   accountFetchedAt() const { return m_accountAt; }
    QVector<CloudModel> models() const { return m_models; }

    // Min-client: ha bármely válasz X-Tanara-Min-Client-je > saját verzió, vagy 426 jött.
    bool    clientTooOld() const { return m_tooOld; }
    QString minClient() const { return m_minClient; }
    QString downloadUrl() const { return m_downloadUrl; }

    // A gateway-hívások közös fejlécei (Authorization NÉLKÜL — azt a provider teszi hozzá).
    // jobId / summaryMode üres → a fejléc kimarad.
    QList<QPair<QByteArray, QByteArray>> requestHeaders(const QString& jobId = QString(),
                                                         const QString& summaryMode = QString()) const;

    // Egy gateway-válasz közös feldolgozása: min-client, egyenleg a terhelés-fejlécekből.
    void observeExchange(const HttpExchange& ex);

    // ---- függő átírások (visszaírás-ellenőrzés indításkor, CLI-13) ----
    void addPendingTranscription(const QString& transcriptionId, const QString& fileId);
    void removePendingTranscription(const QString& transcriptionId);
    QStringList pendingTranscriptions() const { return m_pending.keys(); }

    // ---- helyi UI-állapot (a cloud-state.json-ban) ----
    QDateTime termsPostponedAt() const { return m_termsPostponed; }
    void      postponeTerms();                         // „Később” (naponta legfeljebb egyszer)
    QDateTime lowBannerDismissedAt() const { return m_lowDismissed; }
    void      dismissLowBanner();
    bool      isNoticeDismissed(const QString& id) const { return m_dismissedNotices.contains(id); }
    void      dismissNotice(const QString& id);

    // ---- hívások (aszinkron; eredmény a jelekben) ----
    void startDeviceFlow();
    void cancelDeviceFlow();
    void logout();                                     // a helyi kulcs hibánál is törlődik
    void refreshAccount();
    void fetchModels();
    void estimate(const EstimateRequest& req,
                  std::function<void(const EstimateResult&, const CloudError&)> done);
    void acceptTerms(const QString& version);
    void createTopupLink();
    void joinWaitlist(const WaitlistSignup& signup);   // auth nélkül (teaser mód is)
    void checkPendingTranscriptions();

    // Kézi kulcs (dashboardról, W-14) — CLI / teszt.
    void setManualApiKey(const QString& key, const QString& email = QString());

signals:
    void deviceCodeReady(const tanara::DeviceCode& code);
    void deviceFlowSucceeded(const QString& email);
    void deviceFlowFailed(const tanara::CloudError& error);   // access_denied / expired_token / hálózat …
    void loggedIn();
    void loggedOut();
    void accountUpdated(const tanara::AccountInfo& account);
    void accountFailed(const tanara::CloudError& error);
    void modelsUpdated();
    void modelsFailed(const tanara::CloudError& error);
    void balanceChanged(const tanara::Money& balance);
    void termsAccepted(const tanara::TermsStatus& terms);
    void termsFailed(const tanara::CloudError& error);
    void topupLinkReady(const QString& url);
    void topupFailed(const tanara::CloudError& error);   // TopupUnavailable → contactUrl
    void waitlistJoined(const QString& email);
    void waitlistFailed(const tanara::CloudError& error);
    void clientTooOldDetected(const QString& minClient);
    // Indításkori ellenőrzés: egy korábbi (félbemaradt) átírás hibával zárult, a díjat visszaírták.
    void previousTranscriptionRefunded(const tanara::Money& refund, const tanara::Money& balance);

private:
    using Done = std::function<void(const HttpExchange&)>;
    void send(const QByteArray& method, const QString& path, const QJsonObject* body,
              bool authenticated, Done done);
    void pollDeviceToken();
    void finishDeviceFlow();
    void loadState();
    void saveState() const;

    KeyStore*   m_keys = nullptr;
    QString     m_statePath;
    QString     m_baseUrl;
    QString     m_lang = QStringLiteral("hu");
    QNetworkAccessManager* m_nam = nullptr;

    QString     m_email;
    AccountInfo m_account;
    QDateTime   m_accountAt;
    QVector<CloudModel> m_models;
    QJsonObject m_modelsRaw;       // a katalógus nyers alakja (perzisztáláshoz)
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

} // namespace tanara
