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
        // "auto" (vagy ismeretlen érték): a rendszer-locale dönt — magyar rendszer → hu,
        // minden más → en. Az uiLanguages() a preferencia-sorrendet adja.
        lang = QStringLiteral("en");
        const QStringList sys = QLocale::system().uiLanguages();
        for (const QString& l : sys)
            if (l.startsWith(QStringLiteral("hu"))) { lang = QStringLiteral("hu"); break; }
    }

    s_activeLang = lang;
    if (lang == QStringLiteral("hu"))
        return;   // forrásnyelv — nincs mit betölteni

    auto* translator = new QTranslator(QCoreApplication::instance());
    if (translator->load(QStringLiteral(":/i18n/tanara_") + lang)) {
        QCoreApplication::installTranslator(translator);
        qCInfo(lcApp).noquote() << "UI-nyelv:" << lang;
    } else {
        // Nincs beágyazott .qm (pl. fejlesztői build fordítások nélkül) → marad a forrásnyelv.
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
