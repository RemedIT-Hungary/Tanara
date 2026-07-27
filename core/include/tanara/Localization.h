#pragma once
//
// Tanara — UI-nyelv feloldása + QTranslator telepítése.
// A FORRÁSNYELV MAGYAR: "hu" esetén nincs betöltendő fordítás. Más nyelvhez a
// futtatható targetbe ágyazott :/i18n/tanara_<lang>.qm töltődik (qt_add_translations).
//
#include <QString>

namespace tanara {

// A settings.json `uiLanguage` mezője ("auto"|"hu"|"en") alapján telepíti a fordítót
// a QCoreApplication-re. "auto" → a rendszer-locale dönt (nem-magyar → "en").
// A Q(Core)Application megkonstruálása UTÁN, az első UI-elem/kimenet ELŐTT hívd.
// metadataDir üres → ~/.tanara (a SettingsManager defaultja).
void installAppTranslator(const QString& metadataDir = QString());

// Az utolsó installAppTranslator() által ténylegesen beállított nyelv ("hu"/"en").
QString activeUiLanguage();

} // namespace tanara
