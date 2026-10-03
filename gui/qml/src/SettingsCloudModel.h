#pragma once
//
// SettingsCloudModel — a Beállítások Tanara Cloud panelje (B06) és a „Hamarosan”
// várólista-ajánlat háttere.
//
// MINDEN, ami látszik, a tanara::CloudAccount-ból jön (e-mail, egyenleg, becsült órák, ÁFA,
// értesítések, a szintek katalógus-ára) — a nézetmodell semmit nem számol és nem talál ki.
// A szint (Gyors / Pontos) és a „költségbecslés indítás előtt” a SettingsViewModel
// piszkozatába megy (mentéskor érvényes). A bejelentkezés, a feltöltés, az Expert-modell és
// az ÁSZF a meglévő Widgets-folyamatokat nyitja (SettingsDialogs); a kijelentkezés és a
// várólista-feliratkozás közvetlenül a CloudAccount hívása.
//
// Controller nélkül (demó / képernyőkép) kitalált fiókot mutat.
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

    // ---- fiók (élő mód) ----
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountChanged)
    Q_PROPERTY(QString email READ email NOTIFY accountChanged)
    // Az egyenleg a UI nyelvén formázva ("" = még nem ismert); state: ok | low | empty | unknown.
    Q_PROPERTY(QString balanceText READ balanceText NOTIFY accountChanged)
    Q_PROPERTY(QString balanceState READ balanceState NOTIFY accountChanged)
    Q_PROPERTY(QString hoursText READ hoursText NOTIFY accountChanged)     // „≈ 31 óra Pontos · …”
    Q_PROPERTY(QString vatText READ vatText NOTIFY accountChanged)
    Q_PROPERTY(QString topupLabel READ topupLabel NOTIFY accountChanged)
    // Egy sornyi állapot a fiók alatt: { text, tone: info|warn|danger, action: ""|login|topup|update|web, actionLabel }
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY accountChanged)
    // A gateway értesítései: [{ level, text, url }]
    Q_PROPERTY(QVariantList notices READ notices NOTIFY accountChanged)
    Q_PROPERTY(QString termsText READ termsText NOTIFY accountChanged)
    Q_PROPERTY(bool termsPending READ termsPending NOTIFY accountChanged)

    // ---- minőség (a piszkozatból) ----
    Q_PROPERTY(QString sttTier READ sttTier WRITE setSttTier NOTIFY tiersChanged)
    Q_PROPERTY(QString llmTier READ llmTier WRITE setLlmTier NOTIFY tiersChanged)
    Q_PROPERTY(QString sttExpert READ sttExpert NOTIFY tiersChanged)   // Expert-modell neve ("" = nincs)
    Q_PROPERTY(QString llmExpert READ llmExpert NOTIFY tiersChanged)
    Q_PROPERTY(QString sttInfo READ sttInfo NOTIFY tiersChanged)       // ár + figyelmeztetések
    Q_PROPERTY(QString llmInfo READ llmInfo NOTIFY tiersChanged)
    Q_PROPERTY(bool estimateBeforeRun READ estimateBeforeRun WRITE setEstimateBeforeRun NOTIFY tiersChanged)
    // A megbeszélések nyelve az átíráshoz (AppSettings::languageHints első eleme; "" = automatikus).
    Q_PROPERTY(QString meetingLanguage READ meetingLanguage WRITE setMeetingLanguage NOTIFY tiersChanged)
    Q_PROPERTY(QVariantList meetingLanguages READ meetingLanguages NOTIFY tiersChanged)

    // ---- várólista (teaser mód) ----
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
    Q_INVOKABLE void logout();            // a megerősítést a QML kéri
    Q_INVOKABLE void topup();
    Q_INVOKABLE void refresh();           // fiók + katalógus újrakérése
    Q_INVOKABLE void pickExpert(const QString& kind);   // Widgets: CloudModelPickerDialog
    Q_INVOKABLE void openTerms();
    Q_INVOKABLE void openDashboard();
    Q_INVOKABLE void openUsage();
    Q_INVOKABLE void noticeAction();
    Q_INVOKABLE void openLink(const QString& url);
    Q_INVOKABLE void toggleWaitLanguage(const QString& code);
    Q_INVOKABLE void submitWaitlist();
    Q_INVOKABLE void resetWaitlist();     // „Másik címmel iratkozom fel”

    // A controller megváltozott / a piszkozat újratöltődött.
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

    // demó
    bool m_demo = false;
    bool m_demoLoggedIn = true;
    QString m_demoJoined;
};

} // namespace tanara_qml
