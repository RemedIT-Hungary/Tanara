#include "SettingsWidgetsDialogs.h"

#include "PeopleManagerDialog.h"
#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudModelPickerDialog.h"
#include "cloud/CloudTermsDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudAccount.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QUrl>
#include <QWindow>

namespace tanara_gui {

// Egy modális hívás idejére: a megjelenő párbeszédablakok a Beállítások-ablak fölé kerülnek.
struct SettingsWidgetsDialogs::Scope {
    explicit Scope(SettingsWidgetsDialogs* d) : self(d) { ++self->m_active; }
    ~Scope() { --self->m_active; }
    SettingsWidgetsDialogs* self;
};

SettingsWidgetsDialogs::SettingsWidgetsDialogs(tanara::AppController* controller, QObject* parent)
    : tanara_qml::SettingsDialogs(parent), m_controller(controller)
{
    qApp->installEventFilter(this);
}

SettingsWidgetsDialogs::~SettingsWidgetsDialogs()
{
    if (qApp) qApp->removeEventFilter(this);
    if (m_people) m_people->close();
}

void SettingsWidgetsDialogs::setOwnerWindow(QWindow* window)
{
    m_owner = window;
}

bool SettingsWidgetsDialogs::eventFilter(QObject* watched, QEvent* event)
{
    if (m_active > 0 && event->type() == QEvent::Show && m_owner && m_owner->isVisible()) {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog && dialog->isWindow() && !dialog->parentWidget()) {
            if (QWindow* handle = dialog->windowHandle())
                if (!handle->transientParent())
                    handle->setTransientParent(m_owner);
        }
    }
    return tanara_qml::SettingsDialogs::eventFilter(watched, event);
}

QString SettingsWidgetsDialogs::pickFolder(const QString& title, const QString& startDir)
{
    Scope scope(this);
    return QFileDialog::getExistingDirectory(nullptr, title, startDir);
}

void SettingsWidgetsDialogs::openFolder(const QString& path)
{
    if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SettingsWidgetsDialogs::openUrl(const QString& url)
{
    cloudui::openUrl(url);
}

void SettingsWidgetsDialogs::openPeople()
{
    if (m_peopleOpener) {
        m_peopleOpener();
        return;
    }
    // Nem-modális, egy példány (mint a főablak hídjánál).
    if (!m_people) {
        m_people = new PeopleManagerDialog(m_controller, nullptr);
        m_people->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_people->show();
    m_people->raise();
    m_people->activateWindow();
}

bool SettingsWidgetsDialogs::cloudLogin()
{
    Scope scope(this);
    CloudLoginDialog dlg(m_controller, nullptr);
    return dlg.exec() == QDialog::Accepted;
}

void SettingsWidgetsDialogs::cloudTopup()
{
    Scope scope(this);
    cloudui::startTopup(nullptr, m_controller);
}

bool SettingsWidgetsDialogs::cloudPickModel(const QString& kind)
{
    Scope scope(this);
    CloudModelPickerDialog dlg(m_controller, kind, nullptr);
    return dlg.exec() == QDialog::Accepted;
}

bool SettingsWidgetsDialogs::cloudTerms()
{
    if (!m_controller || !m_controller->cloud()) return false;
    Scope scope(this);
    const tanara::TermsStatus t = m_controller->cloud()->account().terms;
    if (t.needsAcceptance()) {
        tanara::CloudError e;
        e.kind = tanara::CloudErrorKind::TermsRequired;
        return CloudTermsDialog::requireFromError(m_controller, e, nullptr);
    }
    if (t.needsEarlyAcceptance())
        return CloudTermsDialog::offerEarly(m_controller, nullptr);
    cloudui::openUrl(t.currentUrl);   // nincs elfogadnivaló: csak megmutatjuk
    return false;
}

} // namespace tanara_gui
