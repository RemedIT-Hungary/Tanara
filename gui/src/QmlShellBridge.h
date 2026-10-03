#pragma once
//
// QmlShellBridge — az új (Qt Quick) főablak hídja a Qt Widgets világhoz (App.bridge).
//
// Ami a régi MainWindow-ban Widgets-hez kötött viselkedés volt, itt él tovább:
//  - Beállítások / Személyek párbeszédablak;
//  - a felvevő megnyitása és a `tanara --record` továbbított kérései — a ShellRecorderHost-on
//    át (az burkolja az új QML-felvevőt), a felvevő kérései (megnyitás az elemzőben, rögzítés
//    beállításai), valamint a felvétel végének ablak-kezelése (a rejtett főablak
//    visszahozása, „Leállítom és kilépek”);
//  - a Tanara Cloud folyamatai a gui/src/cloud ablakaival: indulási ellenőrzések (K-01
//    módválasztás, fiók / modellek frissítése, félbemaradt átírás), K-06 becslés-megerősítés,
//    bejelentkezés, akadályok (feltöltés / frissítés), hibaablakok (részleges terheléssel),
//    terhelés / visszaírás értesítés, ÁSZF, túl régi kliens, egyenleg-chip és sávok.
// A QML-oldal a tanara_qml::ShellBridge felületen át éri el (gui/qml/src/ShellBridge.h).
//
#include "ShellBridge.h"

#include <QDateTime>
#include <QPointer>
#include <QStringList>

class QWindow;
class QWidget;

namespace tanara {
class AppController;
struct CloudError;
struct Money;
}

namespace tanara_gui {

class ShellRecorderHost;
class PeopleManagerDialog;

class QmlShellBridge : public tanara_qml::ShellBridge {
    Q_OBJECT
public:
    explicit QmlShellBridge(tanara::AppController* controller, QObject* parent = nullptr);
    ~QmlShellBridge() override;

    // A QML-főablak: a Widgets-ablakok ennek lesznek az (ablakkezelő szerinti) gyerekei, és
    // ebből tudjuk, látszik-e épp a főablak.
    void setMainWindow(QWindow* window);
    // A `tanara --record` továbbított kéréseinek fogadása (a main.cpp hívja; QA-módban nem).
    void startRecorderListening();
    // Egy megbeszélés kijelölése + a főablak előtérbe hozása: a felvevő „Megnyitás az
    // elemzőben” gombja és a `tanara --meeting <id>` (induláskor / másik folyamatból átadva).
    // Üres azonosítóval csak a főablak jön előre.
    void showMeeting(const QString& meetingId);
    // A felvevő ablaka (RecorderWindow.qml: .visible, .vm …) — nullptr, amíg nem nyílt meg.
    // A QA-szkripteknek (--shell-script): App.bridge.recorderWindow().
    Q_INVOKABLE QObject* recorderWindow() const;

    // ---- ShellBridge ----
    bool cloudChipVisible() const override { return m_chipVisible; }
    QString cloudChipText() const override { return m_chipText; }
    QString cloudChipTone() const override { return m_chipTone; }
    QString cloudChipToolTip() const override { return m_chipToolTip; }
    QVariantList cloudBanners() const override { return m_banners; }

    void openSettings(const QString& page) override;
    void openPeople() override;
    void openRecorder() override;
    QString pickAudioFile() override;
    bool handleCloudBlocker(const tanara::ReadinessResult& blocker) override;
    bool confirmCloudEstimate(const QString& meetingId, const QString& task,
                              const QString& mode) override;
    QString identifyParticipantsPreview(const QString& meetingId, bool* cancelled) override;
    void cloudBannerAction(const QString& key) override;
    void cloudBannerDismiss(const QString& key) override;
    void continueRecordingInBackground() override;
    void stopRecordingAndQuit() override;
    void shutdown() override;
    void windowShown() override;
    void windowActivated() override;
    void openUsageLog() override;

protected:
    // A szülő nélküli Widgets-párbeszédablakokat a QML-főablak fölé rendeli (transient parent).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool mainWindowHidden() const;

    // ---- Tanara Cloud ----
    void wireCloud();
    void startupCloudChecks();
    void refreshCloudChrome();
    bool cloudLogin();
    void cloudToast(const QString& text, const QString& requestId = QString(),
                    bool usageLink = true);
    void onCloudCharged(const QString& meetingId, const QString& kind, const tanara::Money& total,
                        int calls, const tanara::Money& balance, const QString& vatMode);
    void onCloudRefunded(const QString& meetingId, const tanara::Money& refund,
                         const tanara::Money& balance, const QString& requestId);
    void onCloudError(const QString& meetingId, const QString& kind, const tanara::CloudError& e,
                      const tanara::Money& charged);

    tanara::AppController* m_controller = nullptr;
    QPointer<QWindow> m_window;

    ShellRecorderHost* m_recorder = nullptr;   // a felvevő-kötés
    QPointer<PeopleManagerDialog> m_peopleDialog;
    bool m_quitAfterStop = false;
    bool m_shutDown = false;

    bool m_chipVisible = false;
    QString m_chipText, m_chipTone, m_chipToolTip;
    QVariantList m_banners;
    bool m_tooOldShown = false;
    bool m_termsOffered = false;
    bool m_cloudStartupDone = false;
    QDateTime m_lastCloudRefresh;
};

} // namespace tanara_gui
