#pragma once
//
// UI language resolution + QTranslator installation.
// The SOURCE LANGUAGE IS HUNGARIAN: for "hu" no translation is loaded. For other languages
// :/i18n/tanara_<lang>.qm, embedded in the executable target (qt_add_translations), is loaded.
#include <QString>

namespace tanara {

// Installs the translator on the QCoreApplication for the given UI language (the `uiLanguage` field
// of settings.json: "auto"|"hu"|"en"; the caller reads it). "auto" (or an unknown value) = the system
// locale decides (non-Hungarian = "en").
// Call it AFTER constructing the Q(Core)Application and BEFORE the first UI element/output.
void installAppTranslator(const QString& uiLanguage);

// The language actually set by the last installAppTranslator() ("hu"/"en").
QString activeUiLanguage();

} // namespace tanara
