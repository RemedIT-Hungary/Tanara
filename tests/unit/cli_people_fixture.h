#pragma once
// CLI tests: a people.json (v3, keyed by PersonId) in the isolated TANARA_HOME, so that the
// made-up meetings can refer to people by id (speakerMap raw -> PersonId).
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace cli_fixture {

// people: person id -> display name.
inline bool writePeople(const QString& home, const QMap<QString, QString>& people)
{
    QJsonObject ps;
    for (auto it = people.constBegin(); it != people.constEnd(); ++it)
        ps.insert(it.key(), QJsonObject{{"name", it.value()}});
    QDir().mkpath(home);
    QFile f(QDir(home).filePath("people.json"));     // TANARA_HOME is the data folder too
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(QJsonDocument(QJsonObject{{"version", 3}, {"people", ps}}).toJson()) > 0;
}

} // namespace cli_fixture
