#include "TagInputModel.h"

#include "AppContext.h"
#include "TagBackend.h"

#include <QVariantMap>

namespace tanara_qml {

TagInputModel::TagInputModel(QObject* parent) : QObject(parent)
{
    m_controller = AppContext::instance()->controllerObject();
    m_ownBackend = createTagBackend(m_controller, this);
    attach(m_ownBackend);
}

void TagInputModel::setController(QObject* controller)
{
    if (m_controller == controller && m_backend == m_ownBackend) return;
    m_controller = controller;
    TagBackend* old = m_ownBackend;
    m_ownBackend = createTagBackend(controller, this);
    attach(m_ownBackend);
    if (old) old->deleteLater();
    emit controllerChanged();
}

void TagInputModel::setBackend(TagBackend* backend)
{
    attach(backend ? backend : m_ownBackend);
}

void TagInputModel::attach(TagBackend* backend)
{
    disconnect(m_conn);
    m_backend = backend;
    if (m_backend) m_conn = connect(m_backend, &TagBackend::tagsChanged, this, &TagInputModel::rebuild);
    rebuild();
}

void TagInputModel::setText(const QString& text)
{
    if (m_text == text) return;
    m_text = text;
    emit textChanged();
    rebuild();
}

void TagInputModel::setLimit(int limit)
{
    limit = std::max(1, limit);
    if (m_limit == limit) return;
    m_limit = limit;
    emit limitChanged();
    rebuild();
}

void TagInputModel::setExcludeIds(const QStringList& ids)
{
    if (m_exclude == ids) return;
    m_exclude = ids;
    emit excludeIdsChanged();
    rebuild();
}

QString TagInputModel::mode() const
{
    if (tagmatch::normalizeName(m_text).isEmpty()) return QStringLiteral("recent");
    for (const tagmatch::InputRow& r : m_rows)
        if (r.kind == QLatin1String("nearDuplicate")) return QStringLiteral("similar");
    return QStringLiteral("typing");
}

void TagInputModel::rebuild()
{
    m_rows = m_backend ? tagmatch::inputRows(m_backend->tags(), m_backend->recent(m_limit), m_text, m_limit, m_exclude)
                       : QVector<tagmatch::InputRow>();
    m_rowsVariant.clear();
    for (const tagmatch::InputRow& r : std::as_const(m_rows)) {
        QVariantMap row;
        row.insert(QStringLiteral("kind"), r.kind);
        row.insert(QStringLiteral("id"), r.id);
        row.insert(QStringLiteral("name"), r.name);
        row.insert(QStringLiteral("count"), r.count);
        row.insert(QStringLiteral("matchStart"), r.matchStart);
        row.insert(QStringLiteral("matchLen"), r.matchLen);
        // A kiemeléshez három darabban (a QML így rich text nélkül rajzolja).
        const bool hit = r.matchStart >= 0 && r.matchLen > 0 && r.matchStart + r.matchLen <= r.name.size();
        row.insert(QStringLiteral("before"), hit ? r.name.left(r.matchStart) : r.name);
        row.insert(QStringLiteral("match"), hit ? r.name.mid(r.matchStart, r.matchLen) : QString());
        row.insert(QStringLiteral("after"), hit ? r.name.mid(r.matchStart + r.matchLen) : QString());
        m_rowsVariant << row;
    }
    emit rowsChanged();
    // Mindig az első sor a kijelölt: a legutóbbi / a legjobb találat / a hasonló meglévő,
    // vagy — ha nincs találat — az „Új címke” sor.
    const int selected = m_rows.isEmpty() ? -1 : 0;
    if (m_selected != selected) {
        m_selected = selected;
        emit selectedRowChanged();
    }
}

void TagInputModel::setSelectedRow(int row)
{
    row = m_rows.isEmpty() ? -1 : std::clamp(row, 0, int(m_rows.size()) - 1);
    if (m_selected == row) return;
    m_selected = row;
    emit selectedRowChanged();
}

void TagInputModel::move(int delta)
{
    if (m_rows.isEmpty()) return;
    setSelectedRow(m_selected < 0 ? 0 : m_selected + delta);
}

bool TagInputModel::chooseSelected()
{
    return choose(m_selected);
}

bool TagInputModel::choose(int row)
{
    if (row < 0 || row >= m_rows.size()) return false;
    const tagmatch::InputRow& r = m_rows[row];
    const bool isNew = r.kind == QLatin1String("new") || r.kind == QLatin1String("forceNew");
    emit chosen(r.name, isNew);
    return true;
}

} // namespace tanara_qml
