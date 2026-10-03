#pragma once
//
// Tanara QML — a modul indítása a `tanara` exe-ből (és a tesztekből):
// parancssor-értelmezés, motor-előkészítés (betűk, kép-provider), ablak indítása, illetve
// a képernyőkép-mód (egy QML-komponens PNG-be renderelése látható ablak nélkül).
//
// Parancssor (a main.cpp adja át a log-kapcsolóktól megtisztított argumentumokat):
//   --theme system|light|dark       téma (env: TANARA_THEME); alapértelmezés: system
//   --gallery                       a vezérlő-galéria interaktívan (nincs AppController)
//   --demo                          a főablak kitalált mintaadattal (nincs AppController)
//   --qml-shot <ki.png>             képernyőkép-mód; a további kapcsolói:
//       --qml-page <Típus>          a modul egy QML-típusa (alapértelmezés: Main)
//       --size <SZxM>               logikai méret (alapértelmezés: 1280x820)
//       --scale <szorzó>            devicePixelRatio (pl. 1.5, 2); alapértelmezés: 1
//       --qml-prop <név=érték>      kezdő property (ismételhető; az érték JSON vagy szöveg)
//       --delay <ms>                várakozás a mentés előtt (alapértelmezés: 300)
//
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QQmlEngine;
class QQmlApplicationEngine;

namespace tanara_qml {

struct QmlOptions {
    QString theme;                      // "" → TANARA_THEME, annak híján "system"
    bool gallery = false;
    bool demo = false;
    QString shotPath;                   // nem üres → képernyőkép-mód
    QString page;                       // --qml-page ("" → Main, galéria-módban Gallery)
    QSize size{1280, 820};
    qreal scale = 1.0;
    int delayMs = 300;
    QVariantMap props;                  // --qml-prop
    QString error;                      // nem üres → hibás parancssor (a hívó kiírja)

    bool shotMode() const { return !shotPath.isEmpty(); }
    // Igaz, ha ebben a módban NEM szabad AppControllert létrehozni (a user adatai érintetlenek).
    bool withoutController() const { return shotMode() || gallery || demo; }
};

QmlOptions parseQmlOptions(const QStringList& args);

// A Q(Gui)Application megkonstruálása ELŐTT hívandó: Qt Quick-beállítások (stílus, natív
// szövegrajzolás), képernyőkép-módban offscreen platform + szoftveres renderer.
void prepareProcess(const QmlOptions& opts);

// Az App-singleton beállítása a kapcsolókból (téma, demó). A QApplication UTÁN hívandó.
void applyOptions(const QmlOptions& opts);

// Betűk regisztrálása + image://tanara provider. Minden motorra, amely a modult tölti.
void setupEngine(QQmlEngine& engine);

// A modul egy típusának betöltése ablakként. Ha a típus gyökere nem ablak (pl. Gallery),
// egy ApplicationWindow-ba csomagolja. false → nem sikerült (a hibát kiírja).
bool loadPage(QQmlApplicationEngine& engine, const QString& page,
              const QVariantMap& props = {}, const QSize& size = {});

// Képernyőkép-mód: betölt, vár, PNG-t ment, kilép. Visszatérés: folyamat-kilépőkód.
int runShot(const QmlOptions& opts);

} // namespace tanara_qml
