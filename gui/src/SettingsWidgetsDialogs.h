#pragma once
//
// SettingsWidgetsDialogs — a QML Beállítások-ablak Widgets-oldali segítője
// (tanara_qml::SettingsDialogs megvalósítása): natív mappaválasztó, mappa / hivatkozás
// megnyitása, a Személyek ablak, és a Tanara Cloud MEGLÉVŐ modális folyamatai
// (CloudLoginDialog, feltöltés, CloudModelPickerDialog, CloudTermsDialog) — ezeket nem
// írtuk újra QML-ben, itt nyílnak a Beállítások-ablak fölött.
//
// Mindhárom folyamat ezt használja, amelyben a Beállítások megnyílhat: a főablak
// (QmlShellBridge), az önálló felvevő és a `tanara --settings` mód.
//
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

    // A Személyek ablakot más nyitja (a főablak hídja, hogy egy példány legyen).
    // Nélküle (önálló felvevő, `tanara --settings`) saját QML Személyek-ablakot nyit.
    void setPeopleOpener(std::function<void(const QString&)> opener) { m_peopleOpener = std::move(opener); }
    // Ugyanez a Címkék ablakra (a főablak hídja nyitja, hogy a könyvtár-szűrés oda menjen).
    void setTagsOpener(std::function<void(const QString&)> opener) { m_tagsOpener = std::move(opener); }
    // Az „Első lépések” ablakot a főablak hídja nyitja; nélküle (önálló felvevő, `tanara
    // --settings`) a Beállítások hivatkozása nem látszik.
    void setOnboardingOpener(std::function<void()> opener) { m_onboardingOpener = std::move(opener); }
    // A modális Widgets-ablakok ehhez az ablakhoz (a Beállításokhoz) tartozzanak.
    void setOwnerWindow(QWindow* window);

    QString pickFolder(const QString& title, const QString& startDir) override;
    void openFolder(const QString& path) override;
    void openUrl(const QString& url) override;
    void openPeople() override;
    void openPeopleAt(const QString& person) override;
    // A Címkék ablaka (a Beállítások „Címkék kezelése” hivatkozásához; a SettingsDialogs
    // felületre a Beállítások-szelet veszi fel). Főablak nélkül saját ablak, könyvtár-szűrés nélkül.
    void openTags(const QString& tagId = QString());
    bool canOpenOnboarding() const override { return bool(m_onboardingOpener); }
    void openOnboarding() override { if (m_onboardingOpener) m_onboardingOpener(); }
    bool cloudLogin() override;
    void cloudTopup() override;
    bool cloudPickModel(const QString& kind) override;
    bool cloudTerms() override;

protected:
    // Amíg egy itteni hívás fut, a megjelenő (szülő nélküli) Widgets-párbeszédablak a
    // Beállítások-ablakhoz tartozik (fölötte marad), nem a főablakhoz.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Scope;

    tanara::AppController* m_controller = nullptr;
    QPointer<QWindow> m_owner;
    int m_active = 0;                 // futó modális hívások (egymásba ágyazhatók)
    tanara_qml::PeopleWindowHost* m_people = nullptr;
    std::function<void(const QString&)> m_peopleOpener;
    tanara_qml::TagsWindowHost* m_tags = nullptr;
    std::function<void(const QString&)> m_tagsOpener;
    std::function<void()> m_onboardingOpener;
};

} // namespace tanara_gui
