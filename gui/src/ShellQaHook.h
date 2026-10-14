#pragma once
// ShellQaHook: developer QA for the main window: `tanara --shell-script <file.qml>`.
//
// The given QML file loads next to the REAL main window (with a real AppController; always
// run it with a TANARA_HOME sandbox!) and gets two properties:
//   window: the Main.qml window (window.shell, window.player, window.library, window.meetingModel)
//   hook:   this object: screenshot, file check, quit
// So the main window can be driven without a display (QT_QPA_PLATFORM=offscreen) and without
// touching the user's desktop (mouse, focus). In normal use, without the switch, it does nothing.
#include "AppContext.h"
#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTabWidget>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTextStream>

namespace tanara_gui {

class ShellQaHook : public QObject {
    Q_OBJECT
public:
    explicit ShellQaHook(QQuickWindow* window, QObject* parent = nullptr)
        : QObject(parent), m_window(window) {}

    // Saves the main window's image to a PNG. false on failure.
    Q_INVOKABLE bool grab(const QString& path)
    {
        if (!m_window) return false;
        const QImage img = m_window->grabWindow();
        return !img.isNull() && img.save(path);
    }
    // Saves the image of ANOTHER QML window (Settings: App.bridge.settingsWindow(), recorder:
    // App.bridge.recorderWindow()) to a PNG; they live in their own engine, which QML's
    // Item.grabToImage cannot reach.
    // QA: finds a QML item by objectName in the main window's tree (e.g. "transcriptTab").
    Q_INVOKABLE QObject* findObject(const QString& objectName) const
    {
        if (!m_window) return nullptr;
        if (m_window->objectName() == objectName) return m_window;
        return m_window->findChild<QObject*>(objectName);
    }
    Q_INVOKABLE bool grabWindow(QObject* window, const QString& path)
    {
        auto* w = qobject_cast<QQuickWindow*>(window);
        if (!w || !w->isVisible()) return false;
        const QImage img = w->grabWindow();
        return !img.isNull() && img.save(path);
    }
    Q_INVOKABLE bool fileExists(const QString& path) const { return QFileInfo::exists(path); }
    // One line to standard output (the script's log).
    Q_INVOKABLE void log(const QString& line) const { QTextStream(stdout) << line << Qt::endl; }
    // ---- Inspecting Widgets windows (the Tanara Cloud dialogs) ----
    // The visible top-level widgets: [{ cls, title, modal, tab (QTabWidget's current page) }].
    Q_INVOKABLE QVariantList widgets() const
    {
        QVariantList out;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (!w->isVisible()) continue;
            QVariantMap m{{QStringLiteral("cls"), QString::fromLatin1(w->metaObject()->className())},
                          {QStringLiteral("title"), w->windowTitle()},
                          {QStringLiteral("modal"), w->isModal()}};
            if (auto* tabs = w->findChild<QTabWidget*>())
                m.insert(QStringLiteral("tab"), tabs->tabText(tabs->currentIndex()));
            out.append(m);
        }
        return out;
    }
    // Presses a button of the first visible window of class `cls`, by its label (or part of it).
    Q_INVOKABLE bool clickButton(const QString& cls, const QString& textPart)
    {
        QWidget* w = widgetOf(cls);
        if (!w) return false;
        for (QAbstractButton* b : w->findChildren<QAbstractButton*>()) {
            if (!b->isVisible() || !b->text().contains(textPart, Qt::CaseInsensitive)) continue;
            QMetaObject::invokeMethod(b, "click", Qt::QueuedConnection);
            return true;
        }
        return false;
    }
    // Fills the window's n-th VISIBLE, editable text field (from 0). false = none.
    Q_INVOKABLE bool fillLineEdit(const QString& cls, int n, const QString& text)
    {
        QWidget* w = widgetOf(cls);
        if (!w) return false;
        int i = 0;
        for (QLineEdit* e : w->findChildren<QLineEdit*>()) {
            if (!e->isVisible() || e->isReadOnly() || qobject_cast<QComboBox*>(e->parentWidget())) continue;
            if (i++ != n) continue;
            e->setText(text);
            return true;
        }
        return false;
    }
    // The number of visible text fields in the window and the indexes of the password fields (diagnostics).
    Q_INVOKABLE QString describeFields(const QString& cls) const
    {
        QWidget* w = widgetOf(cls);
        if (!w) return QStringLiteral("-");
        QStringList out;
        int i = 0;
        for (QLineEdit* e : w->findChildren<QLineEdit*>()) {
            if (!e->isVisible() || e->isReadOnly() || qobject_cast<QComboBox*>(e->parentWidget())) continue;
            out << QStringLiteral("%1:%2%3").arg(i++).arg(e->placeholderText().left(24),
                       e->echoMode() == QLineEdit::Normal ? QString() : QStringLiteral("[pw]"));
        }
        return out.join(QStringLiteral(" | "));
    }
    Q_INVOKABLE bool closeWidget(const QString& cls)
    {
        QWidget* w = widgetOf(cls);
        if (!w) return false;
        QMetaObject::invokeMethod(w, "close", Qt::QueuedConnection);
        return true;
    }
    // The People window calls the same: renames a person in core (oldName: the person's name or PersonId).
    Q_INVOKABLE void renamePerson(const QString& oldName, const QString& newName)
    {
        auto* c = tanara_qml::AppContext::instance()->controller();
        if (!c || !c->people()) return;
        c->people()->refresh();
        const tanara::PersonId person = c->people()->choiceFor(oldName).id;
        if (!person.isNull()) c->renamePerson(person, newName);
    }
    Q_INVOKABLE QStringList knownPeople() const
    {
        auto* c = tanara_qml::AppContext::instance()->controller();
        return c ? c->knownPeople() : QStringList();
    }
    Q_INVOKABLE QString clipboardText() const { return QGuiApplication::clipboard()->text(); }
    // Synthesized key / click for the main window (not the desktop). It goes straight to the
    // window, so the focused item's Keys handlers run but Shortcuts do NOT (those belong to the
    // QTest tests in tests/ui).
    Q_INVOKABLE void key(int key, int modifiers = 0, const QString& text = QString())
    {
        if (!m_window) return;
        QKeyEvent press(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers), text);
        QCoreApplication::sendEvent(m_window, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::KeyboardModifiers(modifiers), text);
        QCoreApplication::sendEvent(m_window, &release);
    }
    Q_INVOKABLE void click(qreal x, qreal y, int button = Qt::LeftButton, int modifiers = 0)
    {
        if (!m_window) return;
        const QPointF pos(x, y), global = m_window->mapToGlobal(pos);
        const auto b = Qt::MouseButton(button);
        const auto mods = Qt::KeyboardModifiers(modifiers);
        QMouseEvent press(QEvent::MouseButtonPress, pos, global, b, b, mods);
        QCoreApplication::sendEvent(m_window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, global, b, Qt::NoButton, mods);
        QCoreApplication::sendEvent(m_window, &release);
    }
    Q_INVOKABLE void resize(int w, int h) { if (m_window) m_window->resize(w, h); }

    // Quit with the given code, bypassing the window's close guard (QA).
    Q_INVOKABLE void quit(int code = 0) { QCoreApplication::exit(code); }

private:
    static QWidget* widgetOf(const QString& cls)
    {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (w->isVisible() && QString::fromLatin1(w->metaObject()->className()).endsWith(cls))
                return w;
        return nullptr;
    }
    QPointer<QQuickWindow> m_window;
};

} // namespace tanara_gui
