#pragma once
//
// Tanara QML: starting the module from the `tanara` exe (and from tests): command-line
// parsing, engine setup (fonts, image provider), starting the window, or the screenshot mode
// (rendering a QML component to PNG without a visible window).
//
// Command line (main.cpp passes the arguments cleaned of the log switches):
//   --theme system|light|dark       theme (env: TANARA_THEME); default: system
//   --gallery                       the control gallery, interactive (no AppController)
//   --demo                          the main window with fictional sample data (no AppController)
//   --qml-shot <out.png>            screenshot mode; its further switches:
//       --qml-page <Type>           a QML type of the module (default: Main)
//       --size <WxH>                logical size (default: 1280x820)
//       --scale <factor>            devicePixelRatio (e.g. 1.5, 2); default: 1
//       --qml-prop <name=value>     initial property (repeatable; the value is JSON or text)
//       --delay <ms>                wait before saving (default: 300)
//
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QQmlEngine;
class QQmlApplicationEngine;

namespace tanara_qml {

struct QmlOptions {
    QString theme;                      // "" -> TANARA_THEME, failing that "system"
    bool gallery = false;
    bool demo = false;
    QString shotPath;                   // non-empty -> screenshot mode
    QString page;                       // --qml-page ("" -> Main, in gallery mode Gallery)
    QSize size{1280, 820};
    qreal scale = 1.0;
    int delayMs = 300;
    QVariantMap props;                  // --qml-prop
    QString error;                      // non-empty -> bad command line (the caller prints it)

    bool shotMode() const { return !shotPath.isEmpty(); }
    // True if an AppController MUST NOT be created in this mode (the user's data stays untouched).
    bool withoutController() const { return shotMode() || gallery || demo; }
};

QmlOptions parseQmlOptions(const QStringList& args);

// To call BEFORE constructing the Q(Gui)Application: Qt Quick settings (style); in screenshot mode the offscreen platform + software renderer.
void prepareProcess(const QmlOptions& opts);

// Picks the Qt Quick text render type from the primary screen's device pixel ratio (native at an
// integer DPR, distance-field at a fractional one); TANARA_TEXT_RENDER=native|qt|auto overrides.
// After the QGuiApplication, before any window; applyOptions() calls it.
void applyTextRendering();

// Sets up the App singleton from the switches (theme, demo). To call AFTER the QApplication.
void applyOptions(const QmlOptions& opts);

// Registers the fonts + the image://tanara provider. For every engine that loads the module.
void setupEngine(QQmlEngine& engine);

// Loads one of the module's types as a window. If the type's root is not a window (e.g.
// Gallery), it wraps it in an ApplicationWindow. False -> failed (prints the error).
bool loadPage(QQmlApplicationEngine& engine, const QString& page,
              const QVariantMap& props = {}, const QSize& size = {});

// Screenshot mode: loads, waits, saves the PNG, exits. Returns: the process exit code.
int runShot(const QmlOptions& opts);

} // namespace tanara_qml
