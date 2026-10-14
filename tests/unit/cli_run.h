#pragma once
// CLI tests: runs the real tanara-cli (TANARA_CLI_EXE) with an isolated TANARA_HOME.
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace cli_fixture {

struct Run { int code = -1; QString out; QString err; };

inline Run runCli(const QString& home, const QStringList& args)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TANARA_HOME", home);
    env.insert("TANARA_CLOUD", "off");
    p.setProcessEnvironment(env);
    p.start(QStringLiteral(TANARA_CLI_EXE), args);
    Run r;
    if (!p.waitForFinished(120000)) { p.kill(); return r; }
    r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    return r;
}

} // namespace cli_fixture
