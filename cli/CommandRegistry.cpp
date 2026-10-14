#include "CommandRegistry.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/app/StartupDiagnostics.h"
#include "tanara/foundation/Version.h"
#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace tanara::cli {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString plural(qint64 n, const char* one, const char* many)
{
    return QString::fromUtf8(n == 1 ? one : many).replace(QStringLiteral("%n"), QString::number(n));
}

namespace {

int editDistance(const QString& a, const QString& b)
{
    QVector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

void printUsage(const Command& c)
{
    out() << c.name << " - " << c.summary << "\n\nUsage:\n";
    for (const QString& line : c.usage.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        out() << "  tanara-cli " << line << "\n";
}

} // namespace

void CommandRegistry::add(Command command)
{
    if (!m_modules.contains(command.module)) m_modules << command.module;
    m_commands.append(std::move(command));
}

const Command* CommandRegistry::find(const QString& name) const
{
    for (const Command& c : m_commands)
        if (c.name == name) return &c;
    return nullptr;
}

QString CommandRegistry::closestName(const QString& typed) const
{
    const QString t = typed.toLower();
    QString best;
    int bestDist = 1 << 20;
    for (const Command& c : m_commands) {
        int d = editDistance(t, c.name);
        // A typed prefix of a command ("recor") is a near miss too.
        if (!t.isEmpty() && c.name.startsWith(t)) d = std::min(d, 1);
        if (d < bestDist) { bestDist = d; best = c.name; }
    }
    return bestDist <= std::max(2, int(t.size()) / 3) ? best : QString();
}

int CommandRegistry::help(const QStringList& args) const
{
    // args: [program, "help", ...]
    const QStringList rest = args.mid(2);
    if (rest.contains(QStringLiteral("--json"))) {
        QJsonArray list;
        for (const Command& c : m_commands)
            list.append(QJsonObject{{"name", c.name}, {"module", c.module}, {"summary", c.summary},
                                    {"usage", c.usage}, {"needsServices", c.needsServices}});
        out() << QJsonDocument(list).toJson(QJsonDocument::Indented);
        out().flush();
        return 0;
    }
    const QString topic = rest.value(0);
    if (!topic.isEmpty()) {
        if (const Command* c = find(topic)) {
            printUsage(*c);
            out().flush();
            return 0;
        }
        if (m_modules.contains(topic)) {
            out() << topic << "\n";
            for (const Command& c : m_commands)
                if (c.module == topic) out() << "  " << c.name.leftJustified(16) << " " << c.summary << "\n";
            out().flush();
            return 0;
        }
        err() << "Unknown command: " << topic << "\n";
        const QString near = closestName(topic);
        if (!near.isEmpty()) err() << "Did you mean: " << near << "?\n";
        err().flush();
        return 2;
    }
    out() << "tanara-cli " << libraryVersion() << ": headless driver of Tanara (English only)\n\n"
             "Usage: tanara-cli <command> [arguments]   (global: --debug, --log-level <level>)\n";
    for (const QString& module : m_modules) {
        out() << "\n" << module << "\n";
        for (const Command& c : m_commands)
            if (c.module == module) out() << "  " << c.name.leftJustified(16) << " " << c.summary << "\n";
    }
    out() << "\nRun 'tanara-cli help <command>' for the usage of one command, 'help <module>' for a group.\n";
    out().flush();
    return 0;
}

int CommandRegistry::dispatch(CliContext& ctx, const QStringList& args) const
{
    const QString name = args.value(1);
    if (name.isEmpty() || name == QLatin1String("help") || name == QLatin1String("--help") || name == QLatin1String("-h"))
        return help(QStringList{args.value(0), QStringLiteral("help")} + args.mid(2));
    // `--version` / `version`: the packaging scripts and the AppImage smoke test rely on it.
    if (name == QLatin1String("--version") || name == QLatin1String("-V") || name == QLatin1String("version")) {
        out() << "tanara-cli " << libraryVersion() << "\n";
        out().flush();
        return 0;
    }
    const Command* c = find(name);
    if (!c) {
        err() << "Unknown command: " << name << "\n";
        const QString near = closestName(name);
        if (!near.isEmpty()) err() << "Did you mean: " << near << "?\n";
        err() << "Run 'tanara-cli help' for the list of commands.\n";
        err().flush();
        return 2;
    }
    if (c->needsServices) ctx.app();
    return c->run(ctx, args);
}

CliContext::CliContext(QCoreApplication& qapp, const CommandRegistry& registry)
    : m_qapp(qapp), m_registry(registry) {}

CliContext::~CliContext() = default;

app::AppController& CliContext::app()
{
    if (!m_app) {
        m_app = std::make_unique<app::AppController>();
        // Detailed startup diagnostics only in debug builds (keeps normal command output clean).
        if (currentLogLevel() == LogLevel::Debug) {
            m_app->refreshDevices();
            app::logStartupDiagnostics(*m_app);
        }
    }
    return *m_app;
}

app::Services& CliContext::services() { return *app().services(); }

} // namespace tanara::cli
