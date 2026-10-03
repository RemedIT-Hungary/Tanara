#pragma once
//
// SettingsDialogs — amit a QML Beállítások-ablak a Qt Widgets / az asztal világától kér.
//
// A Beállítások három folyamatban nyílhat meg (főablak, önálló felvevő, `tanara --settings`),
// ezért nem a főablak hídjára (ShellBridge) támaszkodik, hanem erre a kis felületre. A
// megvalósítás a gui/src-ben él (SettingsWidgetsDialogs: natív mappaválasztó, a Tanara Cloud
// meglévő Widgets-ablakai, Személyek); a tesztek ál-megvalósítást adnak. Nélküle (demó,
// képernyőkép) a nézetmodell ezeket a műveleteket kihagyja.
//
#include <QObject>
#include <QString>

namespace tanara_qml {

class SettingsDialogs : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Natív mappaválasztó; üres, ha a felhasználó visszalépett.
    virtual QString pickFolder(const QString& title, const QString& startDir) = 0;
    // Mappa megnyitása a fájlkezelőben / hivatkozás a böngészőben.
    virtual void openFolder(const QString& path) = 0;
    virtual void openUrl(const QString& url) = 0;
    // Személyek kezelése (nem-modális ablak).
    virtual void openPeople() = 0;

    // ---- Tanara Cloud: a meglévő (modális) Widgets-folyamatok ----
    virtual bool cloudLogin() = 0;                        // true = bejelentkezett
    virtual void cloudTopup() = 0;                        // feltöltés-link / „Írj nekünk”
    virtual bool cloudPickModel(const QString& kind) = 0; // Expert mód; kind: stt | llm
    virtual bool cloudTerms() = 0;                        // ÁSZF megtekintése / elfogadása
};

} // namespace tanara_qml
