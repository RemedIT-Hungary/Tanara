#pragma once
//
// CommandRegistry: the command table of tanara-cli. Every module registers its commands
// (cli/commands/<module>.cpp: registerXCommands); main.cpp only builds the context and dispatches.
//
// The CLI is ENGLISH ONLY: command names, placeholders, help and messages are plain literals
// (no tr()); see cli/README.md.
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <functional>
#include <memory>

class QCoreApplication;

namespace tanara::app { class AppController; class Services; }

namespace tanara::cli {

class CliContext;

// One top-level command. `run` gets the FULL argument list (args[0] = program, args[1] = the
// command name, the rest are its arguments), the same shape the commands always had.
struct Command {
    QString name;                  // "devices", "library", ...
    QString module;                // "recording", "library", ... (the help groups by it)
    QString summary;               // one line
    QString usage;                 // synopsis lines (without the program name), separated by '\n'
    bool needsServices = false;    // true: the app services (AppController) are built before run()
    std::function<int(CliContext&, const QStringList&)> run;
};

// Shared output streams and the "%n" plural helper (the CLI does not translate).
QTextStream& out();
QTextStream& err();
QString plural(qint64 n, const char* one, const char* many);

class CommandRegistry {
public:
    // Adds a command; a module's first command fixes the module's place in the help.
    void add(Command command);
    const Command* find(const QString& name) const;
    const QVector<Command>& commands() const { return m_commands; }
    QStringList modules() const { return m_modules; }

    // The closest registered command name (edit distance / prefix); empty if nothing is near.
    QString closestName(const QString& typed) const;

    // Runs args[1]. help / --help / no command print the help; an unknown command prints a suggestion
    // and returns 2.
    int dispatch(CliContext& ctx, const QStringList& args) const;

    // help [<command> | <module>] [--json]; --json is a machine-readable listing (tests, tooling).
    int help(const QStringList& args) const;

private:
    QVector<Command> m_commands;
    QStringList m_modules;
};

// What a command gets besides its arguments. The heavy part (the AppController and its services)
// is built lazily: commands that work on a folder never create it.
class CliContext {
public:
    explicit CliContext(QCoreApplication& qapp, const CommandRegistry& registry);
    ~CliContext();

    QCoreApplication& qapp() const { return m_qapp; }
    const CommandRegistry& registry() const { return m_registry; }

    // The app controller (built on first use; in debug builds it also logs the startup diagnostics).
    // TODO(P12): disappears once every command runs on app::Services alone.
    app::AppController& app();
    // The composition root of the extracted services (owned by the controller).
    app::Services& services();

private:
    QCoreApplication& m_qapp;
    const CommandRegistry& m_registry;
    std::unique_ptr<app::AppController> m_app;
};

} // namespace tanara::cli
