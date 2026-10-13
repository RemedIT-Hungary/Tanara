#pragma once
//
// SettingsDialogs: what the QML Settings window asks of Qt Widgets / the desktop world.
//
// Settings can open in three processes (main window, standalone recorder, `tanara
// --settings`), so it does not rely on the main window's bridge (ShellBridge) but on this
// small interface. The implementation lives in gui/src (SettingsWidgetsDialogs: native folder
// picker, Tanara Cloud's existing Widgets windows, People); tests provide a fake. Without it
// (demo, screenshot) the view model skips these actions.
//
#include <QObject>
#include <QString>

namespace tanara_qml {

class SettingsDialogs : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Native folder picker; empty if the user cancelled.
    virtual QString pickFolder(const QString& title, const QString& startDir) = 0;
    // Opens a folder in the file manager / a link in the browser.
    virtual void openFolder(const QString& path) = 0;
    virtual void openUrl(const QString& url) = 0;
    // Manage people (non-modal window).
    virtual void openPeople() = 0;
    // The same with a person selected (from Settings: the own person).
    virtual void openPeopleAt(const QString& person)
    {
        Q_UNUSED(person);
        openPeople();
    }

    // The "Getting started" window (Settings > General link). Available only where a main window
    // runs (the bridge opens it); not in the standalone recorder or in `tanara --settings` mode.
    virtual bool canOpenOnboarding() const { return false; }
    virtual void openOnboarding() {}

    // ---- Tanara Cloud: the existing (modal) Widgets flows ----
    virtual bool cloudLogin() = 0;                        // true = signed in
    virtual void cloudTopup() = 0;                        // top-up link / "Write to us"
    virtual bool cloudPickModel(const QString& kind) = 0; // Expert mode; kind: stt | llm
    virtual bool cloudTerms() = 0;                        // view / accept the ToS
};

} // namespace tanara_qml
