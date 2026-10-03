#pragma once
//
// Autostart — a hívásfigyelő (tanara-watcher) indítása bejelentkezéskor.
//
// Linux: ~/.config/autostart/tanara-watcher.desktop (XDG autostart). Windows / macOS: még
// nincs megvalósítva (managed() hamis).
//
// VÉDELEM: TANARA_HOME (homokozó / teszt) mellett és Qt teszt-módban SEMMIT nem ír és nem
// töröl — a felhasználó valódi autostart-bejegyzése egy próbafutástól sosem változhat. Ezt a
// szabályt egyetlen helyen, itt tartjuk be (a figyelő és a Beállítások is ezt hívja).
//
#include <QString>

namespace tanara::autostart {

// Az autostart-bejegyzés útja ezen a platformon (üres, ha nincs támogatva).
QString watcherEntryPath();

// Kezelhető-e most a bejegyzés: támogatott platform, nincs TANARA_HOME, nem teszt-mód.
bool managed();

// A figyelő futtatható fájlja a megadott mappához képest (a `tanara` mellett, vagy a
// build-fában ../watcher/). Üres, ha nincs meg.
QString findWatcherExecutable(const QString& applicationDir);

// A bejegyzés be- / kikapcsolása. false és NEM történik semmi, ha !managed(), vagy ha
// bekapcsolásnál a futtatható út üres.
bool applyWatcher(bool on, const QString& watcherExecutable);

// A .desktop fájl megírása egy adott útra (a védelem NÉLKÜL — csak az applyWatcher és a
// tesztek hívják, a tesztek ideiglenes mappával).
bool writeEntry(const QString& entryPath, const QString& watcherExecutable);

} // namespace tanara::autostart
