#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/SettingsManager.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

namespace tanara {

namespace {
QString s_activeLang = QStringLiteral("hu");
}

void installAppTranslator(const QString& metadataDir)
{
    SettingsManager sm(metadataDir);
    QString lang = sm.settings().uiLanguage;

    if (lang != QStringLiteral("hu") && lang != QStringLiteral("en")) {
        // "auto" (or an unknown value): the system locale decides: a Hungarian system -> hu, everything
        // else -> en. uiLanguages() gives the preference order.
        lang = QStringLiteral("en");
        const QStringList sys = QLocale::system().uiLanguages();
        for (const QString& l : sys)
            if (l.startsWith(QStringLiteral("hu"))) { lang = QStringLiteral("hu"); break; }
    }

    s_activeLang = lang;
    if (lang == QStringLiteral("hu"))
        return;   // the source language: nothing to load

    auto* translator = new QTranslator(QCoreApplication::instance());
    if (translator->load(QStringLiteral(":/i18n/tanara_") + lang)) {
        QCoreApplication::installTranslator(translator);
        qCInfo(lcApp).noquote() << "UI-nyelv:" << lang;
    } else {
        // No embedded .qm (e.g. a developer build without translations): the source language stays.
        qCWarning(lcApp).noquote()
            << "Nem található beágyazott fordítás ehhez:" << lang << "— marad a magyar.";
        delete translator;
        s_activeLang = QStringLiteral("hu");
    }
}

QString activeUiLanguage()
{
    return s_activeLang;
}

} // namespace tanara
