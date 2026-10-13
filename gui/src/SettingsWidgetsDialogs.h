#pragma once
// SettingsWidgetsDialogs: the Widgets-side helper of the QML Settings window
// (implementation of tanara_qml::SettingsDialogs): native folder picker, opening a folder /
// link, the People and Tags windows.
//
// All three processes in which Settings can open use it: the main window (QmlShellBridge),
// the standalone recorder and the `tanara --settings` mode.
#include "SettingsDialogs.h"

#include <QPointer>

#include <functional>

class QWindow;

namespace tanara { class AppController; }
namespace tanara_qml { class PeopleWindowHost; class TagsWindowHost; }

namespace tanara_gui {


class SettingsWidgetsDialogs : public tanara_qml::SettingsDialogs {
    Q_OBJECT
public:
    explicit SettingsWidgetsDialogs(tanara::AppController* controller, QObject* parent = nullptr);
    ~SettingsWidgetsDialogs() override;

    // Someone else opens the People window (the main window's bridge, to keep one instance).
    // Without it (standalone recorder, `tanara --settings`) it opens its own QML People window.
    void setPeopleOpener(std::function<void(const QString&)> opener) { m_peopleOpener = std::move(opener); }
    // The same for the Tags window (opened by the main window's bridge, so library filtering goes there).
    void setTagsOpener(std::function<void(const QString&)> opener) { m_tagsOpener = std::move(opener); }
    // The main window's bridge opens the "Getting started" window; without it (standalone
    // recorder, `tanara --settings`) the Settings link is not shown.
    void setOnboardingOpener(std::function<void()> opener) { m_onboardingOpener = std::move(opener); }
    // The modal Widgets windows belong to this window (Settings).
    void setOwnerWindow(QWindow* window);

    QString pickFolder(const QString& title, const QString& startDir) override;
    void openFolder(const QString& path) override;
    void openUrl(const QString& url) override;
    void openPeople() override;
    void openPeopleAt(const QString& person) override;
    // The Tags window (for the Settings "Manage tags" link; the Settings slice adds it to the
    // SettingsDialogs interface). Without a main window it is its own window, without library filtering.
    void openTags(const QString& tagId = QString());
    bool canOpenOnboarding() const override { return bool(m_onboardingOpener); }
    void openOnboarding() override { if (m_onboardingOpener) m_onboardingOpener(); }

protected:
    // While a call here is running, the (parentless) Widgets dialog that appears belongs to the
    // Settings window (stays above it), not to the main window.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Scope;

    tanara::AppController* m_controller = nullptr;
    QPointer<QWindow> m_owner;
    int m_active = 0;                 // running modal calls (can nest)
    tanara_qml::PeopleWindowHost* m_people = nullptr;
    std::function<void(const QString&)> m_peopleOpener;
    tanara_qml::TagsWindowHost* m_tags = nullptr;
    std::function<void(const QString&)> m_tagsOpener;
    std::function<void()> m_onboardingOpener;
};

} // namespace tanara_gui
