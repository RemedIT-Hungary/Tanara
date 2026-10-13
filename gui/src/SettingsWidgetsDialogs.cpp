#include "SettingsWidgetsDialogs.h"

#include "PeopleWindowHost.h"
#include "TagsWindowHost.h"

#include "tanara/app/AppController.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QUrl>
#include <QWindow>

namespace tanara_gui {

// For the duration of a modal call: the dialogs that appear go above the Settings window.
struct SettingsWidgetsDialogs::Scope {
    explicit Scope(SettingsWidgetsDialogs* d) : self(d) { ++self->m_active; }
    ~Scope() { --self->m_active; }
    SettingsWidgetsDialogs* self;
};

SettingsWidgetsDialogs::SettingsWidgetsDialogs(tanara::app::AppController* controller, QObject* parent)
    : tanara_qml::SettingsDialogs(parent), m_controller(controller)
{
    qApp->installEventFilter(this);
}

SettingsWidgetsDialogs::~SettingsWidgetsDialogs()
{
    if (qApp) qApp->removeEventFilter(this);
    if (m_people) m_people->closeNow();
    if (m_tags) m_tags->closeNow();
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
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl::fromUserInput(url));
}

void SettingsWidgetsDialogs::openPeople()
{
    openPeopleAt(QString());
}

void SettingsWidgetsDialogs::openPeopleAt(const QString& person)
{
    if (m_peopleOpener) {
        m_peopleOpener(person);
        return;
    }
    // Process without a main window: its own (non-modal) QML People window, one instance.
    if (!m_people) m_people = new tanara_qml::PeopleWindowHost(m_controller, this);
    m_people->open(person);
}

void SettingsWidgetsDialogs::openTags(const QString& tagId)
{
    if (m_tagsOpener) {
        m_tagsOpener(tagId);
        return;
    }
    if (!m_tags) m_tags = new tanara_qml::TagsWindowHost(m_controller, this);
    m_tags->open(tagId);
}

} // namespace tanara_gui
