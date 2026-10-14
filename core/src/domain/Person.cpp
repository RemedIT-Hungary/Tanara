#include "tanara/domain/Person.h"

#include <QUuid>

namespace tanara {

PersonId PersonId::fromString(const QString& s)
{
    PersonId id;
    id.m_value = s.trimmed();
    return id;
}

PersonId PersonId::generate()
{
    PersonId id;
    id.m_value = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    return id;
}

namespace persons {

QString nameKey(const QString& name)
{
    return name.trimmed().toCaseFolded();
}

bool sameName(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace persons

} // namespace tanara
