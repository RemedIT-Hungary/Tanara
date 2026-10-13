#include "DemoText.h"

#include "tanara/foundation/i18n/Localization.h"

#include <QHash>
#include <QRegularExpression>

namespace tanara_qml::demotext {

namespace {

QString& overrideLang()
{
    static QString lang;
    return lang;
}

// The English counterparts of the fictional Hungarian names (the first-name / surname
// matches are deliberate: where the Hungarian demo relies on shared names, two "Bence"s, two
// "Fehér"s, the English one does too).
const char* const kNames[][2] = {
    {"Kovács Lilla", "Emma Carter"},
    {"Fehér Gábor", "Daniel Hughes"},
    {"Varga Árpád", "James Walker"},
    {"Molnár Eszter", "Sophie Bennett"},
    {"Szabó Bence", "Noah Turner"},
    {"Varga Nóra", "Olivia Grant"},
    {"Fehér Ádám", "Ethan Hughes"},
    {"Tóth Bence", "Ryan Mitchell"},
    {"Bárány Gergely", "Gregory Barnes"},
    {"Bárány Gergő", "Greg Barnes"},
    {"Szabó Áron", "Lucas Reed"},
    {"Kovács Anna", "Anna Carter"},
    {"Nagy Péter", "Peter Hall"},
    {"Gál Bence", "Ryan Gale"},
    {"Tóth Eszter", "Hannah Mitchell"},
    {"Horváth Gergő", "Harry Holt"},
    {"Németh Dávid", "David Norris"},
    {"Balogh Kata", "Kate Baker"},
    {"Nagy Lilla", "Lily Hall"},
    {"Nagy Bence", "Ben Hall"},
    {"Kiss Anna", "Anna Price"},
    {"Varga Tímea", "Tina Walker"},
    {"Szilágyi Bálint", "Blake Shaw"},
    {"Szabó Dóra", "Laura Turner"},
    {"Németh Dóra", "Dana Norris"},
    {"Horváth Gábor", "Gavin Holt"},
    {"Halász Dóra", "Diane Fisher"},
    {"Fazekas Ilona", "Irene Potter"},
    {"Csonka Ferenc", "Frank Stone"},
    {"Antal Benedek", "Benedict Adams"},
    {"Bálint Péter", "Peter Ballard"},
    {"Baranyi Zsófia", "Zoe Barrett"},
    {"Ördög Ödön", "Owen Ogden"},
    {"Lantos Réka", "Rachel Lane"},
    {"Dénes Zsófia", "Zara Dean"},
    {"Albert Noémi", "Naomi Alden"},
    {"Balogh Réka", "Rebecca Bailey"},
    {"Erdei Márton", "Martin Woods"},
    {"B. Gergő", "Greg B."},
};

const QHash<QString, QString>& nameMap()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        for (const auto& p : kNames) m.insert(QString::fromUtf8(p[0]), QString::fromUtf8(p[1]));
        return m;
    }();
    return map;
}

} // namespace

QString language()
{
    if (!overrideLang().isEmpty()) return overrideLang();
    return tanara::activeUiLanguage() == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("hu");
}

bool english()
{
    return language() == QLatin1String("en");
}

void setLanguageOverride(const QString& lang)
{
    overrideLang() = lang;
}

QString name(const QString& hu)
{
    if (!english()) return hu;
    return nameMap().value(hu, hu);
}

QStringList names(const QStringList& hu)
{
    if (!english()) return hu;
    QStringList out;
    out.reserve(hu.size());
    for (const QString& n : hu) out << name(n);
    return out;
}

QString raw(const QString& hu)
{
    if (!english()) return hu;
    static const QRegularExpression re(QStringLiteral("^(Beszélő|Távoli|Mikrofon)( \\d+)?(.*)$"));
    const QRegularExpressionMatch m = re.match(hu);
    if (!m.hasMatch()) return name(hu);
    const QString head = m.captured(1);
    const QString en = head == QStringLiteral("Beszélő") ? QStringLiteral("Speaker")
                     : head == QStringLiteral("Távoli")  ? QStringLiteral("Remote")
                                                          : QStringLiteral("Mic");
    return en + m.captured(2) + m.captured(3);
}

} // namespace tanara_qml::demotext
