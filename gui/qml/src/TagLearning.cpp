#include "TagLearning.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace tanara_qml::taglearning {

QString nameList(const QStringList& names)
{
    if (names.isEmpty()) return {};
    if (names.size() == 1) return names.first();
    return QCoreApplication::translate("TagLearning", "%1 és %2").arg(names.mid(0, names.size() - 1).join(QStringLiteral(", ")), names.last());
}

QString questionText(const tanara::library::PersonTagQuestion& q)
{
    const QString who = nameList(q.names);
    const QString tag = QStringLiteral("#") + q.tagName;
    const bool many = q.people.size() > 1;
    if (q.newPeople)
        return many ? QCoreApplication::translate("TagLearning", "%1 új személyek. Rájuk teszem a %2 címkét?").arg(who, tag)
                    : QCoreApplication::translate("TagLearning", "%1 új személy. Ráteszem a %2 címkét?").arg(who, tag);
    return many ? QCoreApplication::translate("TagLearning", "%1 másodszor van %2 megbeszélésen. Rájuk teszem a címkét?").arg(who, tag)
                : QCoreApplication::translate("TagLearning", "%1 másodszor van %2 megbeszélésen. Ráteszem a címkét?").arg(who, tag);
}

QString answeredText(const tanara::library::PersonTagQuestion& q, bool accepted)
{
    const QString tag = QStringLiteral("#") + q.tagName;
    return accepted ? QCoreApplication::translate("TagLearning", "Címke a személyen: %1 · %2").arg(tag, nameList(q.names))
                    : QCoreApplication::translate("TagLearning", "Nem javasoljuk újra: %1 · %2").arg(tag, nameList(q.names));
}

QVector<tanara::library::PersonTagQuestion> pairs(const QVector<tanara::library::PersonTagQuestion>& questions)
{
    QVector<tanara::library::PersonTagQuestion> out;
    for (const tanara::library::PersonTagQuestion& q : questions)
        for (int i = 0; i < q.people.size(); ++i)
            out.append(tanara::library::PersonTagQuestion{ q.tagId, q.tagName, { q.people.at(i) }, { q.names.value(i) },
                                                           q.newPeople });
    return out;
}

QVariantList pairMaps(const QVector<tanara::library::PersonTagQuestion>& list)
{
    QVariantList out;
    for (int i = 0; i < list.size(); ++i) {
        const tanara::library::PersonTagQuestion& p = list.at(i);
        out << QVariantMap{{QStringLiteral("index"), i},
                           {QStringLiteral("name"), p.names.value(0)},
                           {QStringLiteral("tagName"), p.tagName},
                           {QStringLiteral("text"), p.newPeople ? QCoreApplication::translate("TagLearning", "új személy") : QCoreApplication::translate("TagLearning", "másodszor")}};
    }
    return out;
}

} // namespace tanara_qml::taglearning
