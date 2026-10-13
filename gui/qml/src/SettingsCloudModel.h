#pragma once
//
// SettingsCloudModel: Settings' Tanara Cloud panel and the backend of the "Coming soon"
// waitlist offer.
//
// EVERYTHING shown comes from tanara::CloudAccount (email, balance, estimated hours, VAT,
// notices, the tiers' catalog price); the view model computes and invents nothing. The tier
// (Fast / Accurate) and "cost estimate before start" go into the SettingsViewModel draft
// (effective on save). Sign-in, top-up, the Expert model and the ToS open the existing
// Widgets flows (SettingsDialogs); sign-out and waitlist signup are direct CloudAccount calls.
//
// Without a controller (demo / screenshot) it shows a fictional account.
//
#include "tanara/cloud/CloudTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SettingsViewModel;

class SettingsCloudModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    // ---- account (live mode) ----
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountChanged)
    Q_PROPERTY(QString email READ email NOTIFY accountChanged)
    // The balance formatted in the UI language ("" = not known yet); state: ok | low | empty | unknown.
    Q_PROPERTY(QString balanceText READ balanceText NOTIFY accountChanged)
    Q_PROPERTY(QString balanceState READ balanceState NOTIFY accountChanged)
    Q_PROPERTY(QString hoursText READ hoursText NOTIFY accountChanged)     // "~ 31 hours Accurate · ..."
    Q_PROPERTY(QString vatText READ vatText NOTIFY accountChanged)
    Q_PROPERTY(QString topupLabel READ topupLabel NOTIFY accountChanged)
    // A one-line status under the account: { text, tone: info|warn|danger, action: ""|login|topup|update|web, actionLabel }
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY accountChanged)
    // The gateway's notices: [{ level, text, url }]
    Q_PROPERTY(QVariantList notices READ notices NOTIFY accountChanged)
    Q_PROPERTY(QString termsText READ termsText NOTIFY accountChanged)
    Q_PROPERTY(bool termsPending READ termsPending NOTIFY accountChanged)

    // ---- quality (from the draft) ----
    Q_PROPERTY(QString sttTier READ sttTier WRITE setSttTier NOTIFY tiersChanged)
    Q_PROPERTY(QString llmTier READ llmTier WRITE setLlmTier NOTIFY tiersChanged)
    Q_PROPERTY(QString sttExpert READ sttExpert NOTIFY tiersChanged)   // name of the Expert model ("" = none)
    Q_PROPERTY(QString llmExpert READ llmExpert NOTIFY tiersChanged)
    Q_PROPERTY(QString sttInfo READ sttInfo NOTIFY tiersChanged)       // price + warnings
    Q_PROPERTY(QString llmInfo READ llmInfo NOTIFY tiersChanged)
    Q_PROPERTY(bool estimateBeforeRun READ estimateBeforeRun WRITE setEstimateBeforeRun NOTIFY tiersChanged)
    // The meetings' language for transcription (AppSettings::languageHints first element; "" = automatic).
    Q_PROPERTY(QString meetingLanguage READ meetingLanguage WRITE setMeetingLanguage NOTIFY tiersChanged)
    Q_PROPERTY(QVariantList meetingLanguages READ meetingLanguages NOTIFY tiersChanged)

    // ---- waitlist (teaser mode) ----
    Q_PROPERTY(QString joinedEmail READ joinedEmail NOTIFY waitlistChanged)
    Q_PROPERTY(QString waitEmail READ waitEmail WRITE setWaitEmail NOTIFY waitlistChanged)
    Q_PROPERTY(QString waitUseCase READ waitUseCase WRITE setWaitUseCase NOTIFY waitlistChanged)
    Q_PROPERTY(QVariantList useCases READ useCases CONSTANT)
    Q_PROPERTY(QStringList waitLanguages READ waitLanguages NOTIFY waitlistChanged)
    Q_PROPERTY(bool waitConsent READ waitConsent WRITE setWaitConsent NOTIFY waitlistChanged)
    Q_PROPERTY(bool waitBusy READ waitBusy NOTIFY waitlistChanged)
    Q_PROPERTY(bool waitCanSubmit READ waitCanSubmit NOTIFY waitlistChanged)
    Q_PROPERTY(QString waitError READ waitError NOTIFY waitlistChanged)
    Q_PROPERTY(QString privacyUrl READ privacyUrl CONSTANT)

public:
    explicit SettingsCloudModel(SettingsViewModel* vm);

    bool loggedIn() const;
    QString email() const;
    QString balanceText() const;
    QString balanceState() const;
    QString hoursText() const;
    QString vatText() const;
    QString topupLabel() const;
    QVariantMap notice() const;
    QVariantList notices() const;
    QString termsText() const;
    bool termsPending() const;

    QString sttTier() const;
    void setSttTier(const QString& tier);
    QString llmTier() const;
    void setLlmTier(const QString& tier);
    QString sttExpert() const { return expertName(true); }
    QString llmExpert() const { return expertName(false); }
    QString sttInfo() const { return tierInfo(true); }
    QString llmInfo() const { return tierInfo(false); }
    bool estimateBeforeRun() const;
    void setEstimateBeforeRun(bool on);
    QString meetingLanguage() const;
    void setMeetingLanguage(const QString& code);
    QVariantList meetingLanguages() const;

    QString joinedEmail() const;
    QString waitEmail() const { return m_waitEmail; }
    void setWaitEmail(const QString& email);
    QString waitUseCase() const { return m_waitUseCase; }
    void setWaitUseCase(const QString& useCase);
    QVariantList useCases() const;
    QStringList waitLanguages() const { return m_waitLanguages; }
    bool waitConsent() const { return m_waitConsent; }
    void setWaitConsent(bool on);
    bool waitBusy() const { return m_waitBusy; }
    bool waitCanSubmit() const;
    QString waitError() const { return m_waitError; }
    QString privacyUrl() const;

    Q_INVOKABLE void login();             // Widgets: CloudLoginDialog
    Q_INVOKABLE void logout();            // QML asks for the confirmation
    Q_INVOKABLE void topup();
    Q_INVOKABLE void refresh();           // re-request account + catalog
    Q_INVOKABLE void pickExpert(const QString& kind);   // Widgets: CloudModelPickerDialog
    Q_INVOKABLE void openTerms();
    Q_INVOKABLE void openDashboard();
    Q_INVOKABLE void openUsage();
    Q_INVOKABLE void noticeAction();
    Q_INVOKABLE void openLink(const QString& url);
    Q_INVOKABLE void toggleWaitLanguage(const QString& code);
    Q_INVOKABLE void submitWaitlist();
    Q_INVOKABLE void resetWaitlist();     // "Sign up with another address"

    // The controller changed / the draft was reloaded.
    void attach();
    void notifyDraftChanged() { emit tiersChanged(); emit waitlistChanged(); }
    void loadDemo(const QString& state);

signals:
    void accountChanged();
    void tiersChanged();
    void waitlistChanged();

private:
    QString expertName(bool stt) const;
    QString tierInfo(bool stt) const;
    QString lang() const;

    SettingsViewModel* m_vm;
    QList<QMetaObject::Connection> m_connections;
    tanara::CloudError m_lastError;

    QString m_waitEmail, m_waitUseCase, m_waitError;
    QStringList m_waitLanguages;
    bool m_waitConsent = false;
    bool m_waitBusy = false;

    // demo
    bool m_demo = false;
    bool m_demoLoggedIn = true;
    QString m_demoJoined;
};

} // namespace tanara_qml
