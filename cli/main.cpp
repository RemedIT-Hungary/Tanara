// tanara-cli: headless driver on top of core (NO Widgets).
// The commands live in cli/commands/<module>.cpp and register themselves into the CommandRegistry;
// `tanara-cli help` lists them by module. CLI output is ENGLISH and untranslated; see cli/README.md.
#include "CommandRegistry.h"
#include "commands/Commands.h"

#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QTranslator>

using namespace tanara;

int main(int argc, char** argv)
{
    // Set up logging BEFORE QCoreApplication. The log switches (--debug, --log-level ...) are
    // removed from argv so the command parser gets a clean list.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QFile::decodeName(argv[i]);   // NFC on macOS (QProcess sends NFD)
    initLogging(parseLogOptions(rawArgs));

    QCoreApplication qapp(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Tanara"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tanara"));
    // CLI output is ALWAYS English, regardless of the configured UI language. Its own strings
    // are plain English literals; the English translation is loaded for error messages that
    // come from core (Hungarian source strings).
    {
        auto* translator = new QTranslator(&qapp);
        if (translator->load(QStringLiteral(":/i18n/tanara_en")))
            QCoreApplication::installTranslator(translator);
    }
    const QStringList args = stripLogArgs(rawArgs);

    cli::CommandRegistry registry;
    cli::registerAllCommands(registry);
    // The context outlives nothing but this scope: the lazily built AppController dies before qapp.
    cli::CliContext ctx(qapp, registry);
    return registry.dispatch(ctx, args);
}
