#pragma once
//
// ShellBridge — amit a QML-héj a Qt Widgets világtól kér (App.bridge).
//
// A megvalósítás a gui/src-ben él (QmlShellBridge: az ismeri a Beállítások / Személyek /
// felvevő / Tanara Cloud Widgets-osztályokat), és a main.cpp telepíti:
//   AppContext::instance()->setBridge(bridge)
// Itt csak az absztrakt felület van, hogy a ShellActions (és a tesztek egy ál-híddal) a
// Widgets linkelése nélkül használhassák. Híd nélkül (demó, képernyőkép) a ShellActions
// ezeket a műveleteket kihagyja.
//
#include "tanara/provider/ReadinessModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace tanara_qml {

class ShellBridge : public QObject {
    Q_OBJECT

    // ---- Tanara Cloud „króm” (K-08 / K-10 / notice): egyenleg-chip + sávok ----
    Q_PROPERTY(bool cloudChipVisible READ cloudChipVisible NOTIFY cloudChromeChanged)
    Q_PROPERTY(QString cloudChipText READ cloudChipText NOTIFY cloudChromeChanged)
    // "normal" | "warn" (kevés egyenleg) | "danger" (elfogyott)
    Q_PROPERTY(QString cloudChipTone READ cloudChipTone NOTIFY cloudChromeChanged)
    Q_PROPERTY(QString cloudChipToolTip READ cloudChipToolTip NOTIFY cloudChromeChanged)
    // [{ key, level: "critical"|"warning"|"info", text, cta, closable }]
    Q_PROPERTY(QVariantList cloudBanners READ cloudBanners NOTIFY cloudChromeChanged)

public:
    using QObject::QObject;

    virtual bool cloudChipVisible() const = 0;
    virtual QString cloudChipText() const = 0;
    virtual QString cloudChipTone() const = 0;
    virtual QString cloudChipToolTip() const = 0;
    virtual QVariantList cloudBanners() const = 0;

    // ---- Widgets-párbeszédablakok ----
    // page: "" | "providers" | "watcher" | "cloud" | "summary" | "recording"
    Q_INVOKABLE virtual void openSettings(const QString& page) = 0;
    // Ugyanez mély hivatkozással (B04): focusField "stt" | "llm" — a hiányzó szolgáltató
    // kártyája kiemelve, sikeres mentés után vissza a megbeszéléshez. Az alapértelmezés a
    // sima openSettings (a tesztek ál-hídjainak nem kell tudniuk róla).
    Q_INVOKABLE virtual void openSettingsAt(const QString& page, const QString& focusField)
    {
        Q_UNUSED(focusField);
        openSettings(page);
    }
    Q_INVOKABLE virtual void openPeople() = 0;
    // Ugyanez egy személy kijelölésével (üres név: a korábbi kijelölés marad). Az
    // alapértelmezés a sima openPeople (a tesztek ál-hídjainak nem kell tudniuk róla).
    Q_INVOKABLE virtual void openPeopleAt(const QString& person)
    {
        Q_UNUSED(person);
        openPeople();
    }
    Q_INVOKABLE virtual void openRecorder() = 0;
    // Natív fájlválasztó hangfájlhoz; üres, ha a felhasználó visszalépett.
    Q_INVOKABLE virtual QString pickAudioFile() = 0;
    // Ugyanez több fájlra (importálás): hang- és videófájlok; üres lista = visszalépett.
    Q_INVOKABLE virtual QStringList pickAudioFiles() = 0;

    // ---- Tanara Cloud kapuk (a ShellActions hívja a feldolgozás indítása előtt) ----
    // Egy blokkolt lépés cloud-teendője (bejelentkezés / feltöltés / frissítés). true = a
    // híd kezelte (a hívó ne nyisson Beállításokat). Nem cloud-akadálynál false.
    virtual bool handleCloudBlocker(const tanara::ReadinessResult& blocker) = 0;
    // K-06: költségbecslés + megerősítés cloud-futás előtt. true = indítható. Saját kulcsos
    // (BYO) lépésnél azonnal true. task: transcribe | summarize; mode: "" | quick | complex.
    virtual bool confirmCloudEstimate(const QString& meetingId, const QString& task,
                                      const QString& mode) = 0;

    // ---- Résztvevők tippelése ÁTIRAT ELŐTT (hang-klaszterek + lenyomat-DB) ----
    // Modális, megszakítható haladás-ablakkal fut. Visszaad egy emberi összegző mondatot;
    // megszakításnál üres és *cancelled = true.
    virtual QString identifyParticipantsPreview(const QString& meetingId, bool* cancelled) = 0;

    // ---- sávok műveletei ----
    Q_INVOKABLE virtual void cloudBannerAction(const QString& key) = 0;
    Q_INVOKABLE virtual void cloudBannerDismiss(const QString& key) = 0;

    // ---- felvétel és ablak-életciklus ----
    // A lebegő felvevő megnyitása, a főablak pedig elrejthető (a felvétel a háttérben megy).
    Q_INVOKABLE virtual void continueRecordingInBackground() = 0;
    // A felvétel leállítása; amikor a kódolás kész (Idle), quitRequested() jön.
    Q_INVOKABLE virtual void stopRecordingAndQuit() = 0;
    // A főablak tényleges bezárása előtt: felvevő-ablak, singleton, szintfigyelés lezárása.
    Q_INVOKABLE virtual void shutdown() = 0;
    // A főablak láthatóvá vált (első alkalommal: cloud indulási ellenőrzések).
    Q_INVOKABLE virtual void windowShown() = 0;
    // A főablak újra aktív lett (cloud-egyenleg frissítése feltöltés után).
    Q_INVOKABLE virtual void windowActivated() = 0;

signals:
    void cloudChromeChanged();
    // Nem-modális értesítés (K-07 költség, visszaírás …). requestId: másolható hibaazonosító;
    // usageLink: „Napló a weben” hivatkozás (lásd openUsageLog).
    void toastRequested(const QString& text, const QString& requestId, bool usageLink);
    // Egy cloud-hiba ablakában „Újra” / „Folytatás”: ugyanazt a lépést kell újraindítani.
    // kind: transcribe | summary | topics | complex.
    void retryRequested(const QString& meetingId, const QString& kind);
    // Valami megváltozott, ami a lépések futtathatóságát érinti (beállítások, be-/kilépés,
    // modellek) — a nézetek értékeljék újra a canRun-t.
    void readinessChanged();
    // A Beállításokban mentett téma ("system" | "light" | "dark"): a főablak megjegyzi
    // (ui-state.json), hogy a következő indításkor is ez legyen.
    void themeModeSaved(const QString& mode);
    // Felvétel közben a hívás véget ért / csend van: a héj rákérdez („Vége a meetingnek?”).
    // A QML-felvevővel a híd NEM küldi: a kérdést a felvevő saját doboza (R06) teszi fel,
    // így a felhasználó egyszer kap kérdést. (Felvevő nélküli hídnak marad meg.)
    void stopPromptRequested(const QString& reason);
    // Egy megbeszélést ki kell jelölni és a főablakot előre hozni (a felvevő „Megnyitás az
    // elemzőben” gombja, `tanara --meeting <id>`). Üres azonosító: csak az ablak jön előre.
    void showMeetingRequested(const QString& meetingId);
    // A főablakot újra meg kell mutatni (háttér-felvétel vége, felvevő bezárása).
    void showWindowRequested();
    // „Leállítom és kilépek” után a felvétel lezárult: a főablak bezárható.
    void quitRequested();

public:
    Q_INVOKABLE virtual void openUsageLog() = 0;
};

} // namespace tanara_qml
