#include "TagLearning.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace tanara_qml::taglearning {

namespace {
QString tr(const char* s, int n = -1) { return QCoreApplication::translate("TagLearning", s, nullptr, n); }
}

QString nameList(const QStringList& names)
{
    if (names.isEmpty()) return {};
    if (names.size() == 1) return names.first();
    return tr("%1 és %2").arg(names.mid(0, names.size() - 1).join(QStringLiteral(", ")), names.last());
}

QString questionText(const tanara::PersonTagQuestion& q)
{
    const QString who = nameList(q.people);
    const QString tag = QStringLiteral("#") + q.tagName;
    const bool many = q.people.size() > 1;
    if (q.newPeople)
        return many ? tr("%1 új személyek. Rájuk teszem a %2 címkét?").arg(who, tag)
                    : tr("%1 új személy. Ráteszem a %2 címkét?").arg(who, tag);
    return many ? tr("%1 másodszor van %2 megbeszélésen. Rájuk teszem a címkét?").arg(who, tag)
                : tr("%1 másodszor van %2 megbeszélésen. Ráteszem a címkét?").arg(who, tag);
}

QString answeredText(const tanara::PersonTagQuestion& q, bool accepted)
{
    const QString tag = QStringLiteral("#") + q.tagName;
    return accepted ? tr("Címke a személyen: %1 · %2").arg(tag, nameList(q.people))
                    : tr("Nem javasoljuk újra: %1 · %2").arg(tag, nameList(q.people));
}

QVector<tanara::PersonTagQuestion> pairs(const QVector<tanara::PersonTagQuestion>& questions)
{
    QVector<tanara::PersonTagQuestion> out;
    for (const tanara::PersonTagQuestion& q : questions)
        for (const QString& name : q.people)
            out.append(tanara::PersonTagQuestion{ q.tagId, q.tagName, { name }, q.newPeople });
    return out;
}

QVariantList pairMaps(const QVector<tanara::PersonTagQuestion>& list)
{
    QVariantList out;
    for (int i = 0; i < list.size(); ++i) {
        const tanara::PersonTagQuestion& p = list.at(i);
        out << QVariantMap{{QStringLiteral("index"), i},
                           {QStringLiteral("name"), p.people.value(0)},
                           {QStringLiteral("tagName"), p.tagName},
                           {QStringLiteral("text"), p.newPeople ? tr("új személy") : tr("másodszor")}};
    }
    return out;
}

} // namespace tanara_qml::taglearning
