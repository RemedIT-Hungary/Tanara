#pragma once
//
// ShellQaHook — fejlesztői QA az új főablakhoz: `tanara --shell-script <fájl.qml>`.
//
// A megadott QML-fájl a VALÓDI főablak mellé töltődik (valódi AppController-rel — mindig
// TANARA_HOME-os homokozóval futtasd!), és két property-t kap:
//   window — a Main.qml ablaka (window.shell, window.player, window.library, window.meetingModel)
//   hook   — ez az objektum: képernyőkép, fájl-ellenőrzés, kilépés
// Így a főablak kijelző nélkül (QT_QPA_PLATFORM=offscreen) is végigvezethető, a felhasználó
// asztalának (egér, fókusz) érintése nélkül. Éles használatban a kapcsoló nélkül semmit nem tesz.
//
#include <QCoreApplication>
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
    Q_INVOKABLE bool fileExists(const QString& path) const { return QFileInfo::exists(path); }
    // Egy sor a szabványos kimenetre (a szkript naplója).
    Q_INVOKABLE void log(const QString& line) const { QTextStream(stdout) << line << Qt::endl; }
    // Kilépés a megadott kóddal — az ablak bezárás-védelmét megkerülve (QA).
    Q_INVOKABLE void quit(int code = 0) { QCoreApplication::exit(code); }

private:
    QPointer<QQuickWindow> m_window;
};

} // namespace tanara_gui
