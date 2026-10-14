#include "LibrarySelectionModel.h"

#include "AppContext.h"
#include "LibraryListModel.h"
#include "ShellFormat.h"

#include "tanara/library/ILibraryFacade.h"

#include <QVariantMap>

#include <algorithm>

namespace tanara_qml {

LibrarySelectionModel::LibrarySelectionModel(QObject* parent) : QObject(parent)
{
    // Without an injected facade the model follows the App singleton's services.
    AppContext* ctx = AppContext::instance();
    const auto resolve = [this] {
        if (m_injected) return;
        m_facade = AppContext::instance()->services().library;
        rebuild();
    };
    connect(ctx, &AppContext::controllerChanged, this, resolve);
    connect(ctx, &AppContext::demoChanged, this, resolve);
    m_facade = ctx->services().library;
}

LibrarySelectionModel::LibrarySelectionModel(tanara::library::ILibraryFacade* facade, QObject* parent)
    : QObject(parent), m_facade(facade), m_injected(true)
{
}

void LibrarySelectionModel::setLibrary(LibraryListModel* library)
{
    if (m_library == library) return;
    if (m_library) m_library->disconnect(this);
    m_library = library;
    if (library) {
        // A change of the rows (title, tags) -> the panel's data refreshes too.
        auto refresh = [this] { if (!m_ids.isEmpty()) rebuild(); };
        connect(library, &LibraryListModel::resultChanged, this, refresh);
        connect(library, &QAbstractItemModel::dataChanged, this, refresh);
        connect(library, &QAbstractItemModel::modelReset, this, refresh);
    }
    emit libraryChanged();
    rebuild();
}

void LibrarySelectionModel::setCurrentId(const QString& id)
{
    if (m_currentId == id) return;
    m_currentId = id;
    emit currentIdChanged();
}

void LibrarySelectionModel::toggle(const QString& id)
{
    if (id.isEmpty()) return;
    QStringList next = m_ids;
    // The first Ctrl+click adds to the open meeting.
    if (next.isEmpty() && !m_currentId.isEmpty() && m_currentId != id)
        next << m_currentId;
    if (next.contains(id))
        next.removeAll(id);
    else
        next << id;
    m_anchor = id;
    setIds(next);
}

void LibrarySelectionModel::rangeTo(const QString& id)
{
    if (id.isEmpty()) return;
    const QString anchor = m_anchor.isEmpty() ? m_currentId : m_anchor;
    const int a = m_library && !anchor.isEmpty() ? m_library->indexOfMeeting(anchor) : -1;
    const int b = m_library ? m_library->indexOfMeeting(id) : -1;
    if (a < 0 || b < 0) {
        toggle(id);
        return;
    }
    QStringList next;
    for (int row = std::min(a, b); row <= std::max(a, b); ++row)
        next << m_library->meetingIdAt(row);
    m_anchor = anchor;     // the next Shift+click counts from the same place
    setIds(next);
}

void LibrarySelectionModel::clear()
{
    m_anchor.clear();
    setIds({});
}

void LibrarySelectionModel::selectIds(const QStringList& ids)
{
    m_anchor = ids.isEmpty() ? QString() : ids.last();
    setIds(ids);
}

void LibrarySelectionModel::setIds(const QStringList& ids)
{
    QStringList unique;
    for (const QString& id : ids)
        if (!id.isEmpty() && !unique.contains(id)) unique << id;
    if (unique == m_ids) return;
    m_ids = unique;
    m_ids = orderedIds();
    emit selectionChanged();
    rebuild();
}

QStringList LibrarySelectionModel::orderedIds() const
{
    if (!m_library) return m_ids;
    // The list's order (newest first); what is not visible due to filtering, by start time.
    QVector<QPair<int, QString>> visible;
    QVector<QPair<QDateTime, QString>> hidden;
    for (const QString& id : m_ids) {
        const int row = m_library->indexOfMeeting(id);
        if (row >= 0)
            visible.append({row, id});
        else
            hidden.append({m_library->entryFor(id).startedAt, id});
    }
    std::sort(visible.begin(), visible.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });
    std::stable_sort(hidden.begin(), hidden.end(),
                     [](const auto& x, const auto& y) { return x.first > y.first; });
    QStringList out;
    for (const auto& v : std::as_const(visible)) out << v.second;
    for (const auto& h : std::as_const(hidden)) out << h.second;
    return out;
}

QStringList LibrarySelectionModel::fullTagIds() const
{
    QStringList out;
    for (const QVariant& v : m_tagRows) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("full")).toBool())
            out << r.value(QStringLiteral("id")).toString();
    }
    return out;
}

void LibrarySelectionModel::rebuild()
{
    QVariantList items;
    QStringList order;                       // the tags by first occurrence
    QHash<QString, QString> names;
    QHash<QString, int> on;
    QStringList alive;
    if (m_library) {
        for (const QString& id : std::as_const(m_ids)) {
            const tanara::library::LibraryEntry e = m_library->entryFor(id);
            if (e.id.isEmpty()) continue;    // deleted meanwhile
            alive << id;
            QStringList hashed;
            for (int i = 0; i < e.tagIds.size(); ++i) {
                const QString& tagId = e.tagIds.at(i);
                const QString name = e.tagNames.value(i);
                hashed << QStringLiteral("#") + name;
                if (!names.contains(tagId)) {
                    order << tagId;
                    names.insert(tagId, name);
                }
                ++on[tagId];
            }
            items.append(QVariantMap{{QStringLiteral("id"), e.id},
                                     {QStringLiteral("title"), e.title},
                                     {QStringLiteral("tagsText"), hashed.join(QLatin1Char(' '))},
                                     {QStringLiteral("dateText"), fmt::shortDate(e.startedAt)}});
        }
    }
    const int total = int(alive.size());
    QVariantList rows;
    bool hinted = false;
    for (const QString& tagId : std::as_const(order)) {
        const int n = on.value(tagId);
        const bool full = n == total;
        QString note = full ? tr("mindegyiken rajta van")
                     : n == 1 ? tr("csak egyiken")
                              : tr("%1 megbeszélésen").arg(n);
        // Next to the first partial tag we say what a click does.
        if (!full && !hinted) {
            note += QStringLiteral(" · ") + tr("kattintásra mindegyikre");
            hinted = true;
        }
        rows.append(QVariantMap{{QStringLiteral("id"), tagId},
                                {QStringLiteral("name"), names.value(tagId)},
                                {QStringLiteral("onCount"), n},
                                {QStringLiteral("total"), total},
                                {QStringLiteral("full"), full},
                                {QStringLiteral("countText"), QStringLiteral("%1/%2").arg(n).arg(total)},
                                {QStringLiteral("note"), note}});
    }
    const bool pruned = m_library && alive.size() != m_ids.size();
    if (pruned) m_ids = alive;
    if (items != m_items || rows != m_tagRows) {
        m_items = items;
        m_tagRows = rows;
        emit contentChanged();
    }
    if (pruned) emit selectionChanged();
}

void LibrarySelectionModel::addTagToAll(const QString& nameOrId)
{
    const QString key = nameOrId.trimmed();
    if (key.isEmpty() || m_ids.isEmpty() || !m_facade) return;
    // Creating and attaching together are one undoable step.
    if (!m_facade->addTagToMeetings(m_ids, key, tr("Címke hozzáadása %n megbeszéléshez", nullptr, count())))
        return;
    rebuild();
}

void LibrarySelectionModel::removeTagFromAll(const QString& tagId)
{
    if (tagId.isEmpty() || m_ids.isEmpty() || !m_facade) return;
    if (!m_facade->removeTagFromMeetings(m_ids, tagId, tr("Címke levétele %n megbeszélésről", nullptr, count())))
        return;
    rebuild();
}

} // namespace tanara_qml
