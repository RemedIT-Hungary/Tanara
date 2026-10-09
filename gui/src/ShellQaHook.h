#pragma once
//
// ShellQaHook — fejlesztői QA a főablakhoz: `tanara --shell-script <fájl.qml>`.
//
// A megadott QML-fájl a VALÓDI főablak mellé töltődik (valódi AppController-rel — mindig
// TANARA_HOME-os homokozóval futtasd!), és két property-t kap:
//   window — a Main.qml ablaka (window.shell, window.player, window.library, window.meetingModel)
//   hook   — ez az objektum: képernyőkép, fájl-ellenőrzés, kilépés
// Így a főablak kijelző nélkül (QT_QPA_PLATFORM=offscreen) is végigvezethető, a felhasználó
// asztalának (egér, fókusz) érintése nélkül. Éles használatban a kapcsoló nélkül semmit nem tesz.
//
#include "AppContext.h"
#include "tanara/AppController.h"

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

    // A főablak képe PNG-be. false, ha nem sikerült.
    Q_INVOKABLE bool grab(const QString& path)
    {
        if (!m_window) return false;
        const QImage img = m_window->grabWindow();
        return !img.isNull() && img.save(path);
    }
    // Egy MÁSIK QML-ablak (Beállítások: App.bridge.settingsWindow(), felvevő:
    // App.bridge.recorderWindow()) képe PNG-be — ezek saját motorban élnek, a QML-ből az
    // Item.grabToImage nem éri el őket.
    // QA: egy QML-elem elérése objectName szerint a főablak fájából (pl. "transcriptTab").
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
    // Egy sor a szabványos kimenetre (a szkript naplója).
    Q_INVOKABLE void log(const QString& line) const { QTextStream(stdout) << line << Qt::endl; }
    // ---- Widgets-ablakok (a Tanara Cloud párbeszédablakai) vizsgálata ----
    // A látható felső szintű widgetek: [{ cls, title, modal, tab (QTabWidget aktuális lapja) }].
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
    // Az első látható `cls` osztályú ablak egy gombjának megnyomása a felirata (részlete) alapján.
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
    // Az ablak n-edik LÁTHATÓ, szerkeszthető szövegmezőjének kitöltése (0-tól). false = nincs.
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
    // Az ablak látható szövegmezőinek száma és a jelszó-mezők indexei (diagnosztika).
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
    // A Személyek ablak ugyanezt hívja: személy átnevezése a core-ban.
    Q_INVOKABLE void renamePerson(const QString& oldName, const QString& newName)
    {
        if (auto* c = tanara_qml::AppContext::instance()->controller()) c->renamePerson(oldName, newName);
    }
    Q_INVOKABLE QStringList knownPeople() const
    {
        auto* c = tanara_qml::AppContext::instance()->controller();
        return c ? c->knownPeople() : QStringList();
    }
    Q_INVOKABLE QString clipboardText() const { return QGuiApplication::clipboard()->text(); }
    // Szintetizált billentyű / kattintás a főablaknak (nem az asztalnak). Közvetlenül az ablak
    // kapja, ezért a fókuszált elem Keys-kezelői futnak, de a Shortcut-ok NEM (azokhoz a
    // tests/ui QTest-es tesztjei valók).
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

    // Kilépés a megadott kóddal — az ablak bezárás-védelmét megkerülve (QA).
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
