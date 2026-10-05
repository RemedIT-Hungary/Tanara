#include "LibrarySelectionModel.h"

#include "AppContext.h"
#include "LibraryDemoData.h"
#include "LibraryListModel.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"
#include "tanara/tags/TagService.h"

#include <QVariantMap>

#include <algorithm>

namespace tanara_qml {

LibrarySelectionModel::LibrarySelectionModel(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        rebuild();
    });
    // Demóban a címkék a mintakönyvtárban változnak.
    connect(demo::tagNotifier(), &demo::DemoTagNotifier::changed, this, [this] {
        if (!m_controller) rebuild();
    });
}

void LibrarySelectionModel::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    rebuild();
}

void LibrarySelectionModel::setLibrary(LibraryListModel* library)
{
    if (m_library == library) return;
    if (m_library) m_library->disconnect(this);
    m_library = library;
    if (library) {
        // A sorok (cím, címkék) változása → a panel adatai is frissülnek.
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
    // Az első Ctrl+kattintás a megnyitott megbeszéléshez ad hozzá.
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
    m_anchor = anchor;     // a következő Shift+kattintás ugyanonnan számol
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
    // A lista sorrendje (legújabb elöl); ami a szűrés miatt nem látszik, a kezdési idő szerint.
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
    QStringList order;                       // a címkék első előfordulás szerint
    QHash<QString, QString> names;
    QHash<QString, int> on;
    QStringList alive;
    if (m_library) {
        for (const QString& id : std::as_const(m_ids)) {
            const tanara::LibraryEntry e = m_library->entryFor(id);
            if (e.id.isEmpty()) continue;    // közben törölték
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
        // Az első részleges címke mellett elmondjuk, mit csinál a kattintás.
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
    if (key.isEmpty() || m_ids.isEmpty()) return;
    if (m_controller) {
        tanara::TagService* tags = m_controller->tags();
        if (!tags) return;
        // A létrehozás és a felrakás együtt egy visszavonható lépés.
        tanara::Tag t = tags->tag(key);
        tags->beginGroup(tr("Címke hozzáadása %n megbeszéléshez", nullptr, count()));
        if (!t.isValid()) t = tags->create(key);
        if (t.isValid()) tags->bulkAdd(m_ids, t.id);
        tags->endGroup();
    } else if (AppContext::instance()->demo()) {
        const QString id = demo::tagName(key).isEmpty() ? demo::createTag(key) : key;
        if (id.isEmpty()) return;
        for (const QString& m : std::as_const(m_ids)) {
            QStringList t = demo::tagsOf(m);
            if (!t.contains(id)) demo::setTagsOf(m, t << id);
        }
    }
    rebuild();
}

void LibrarySelectionModel::removeTagFromAll(const QString& tagId)
{
    if (tagId.isEmpty() || m_ids.isEmpty()) return;
    if (m_controller) {
        tanara::TagService* tags = m_controller->tags();
        if (!tags) return;
        tags->beginGroup(tr("Címke levétele %n megbeszélésről", nullptr, count()));
        tags->bulkRemove(m_ids, tagId);
        tags->endGroup();
    } else if (AppContext::instance()->demo()) {
        for (const QString& m : std::as_const(m_ids)) {
            QStringList t = demo::tagsOf(m);
            if (t.removeAll(tagId) > 0) demo::setTagsOf(m, t);
        }
    }
    rebuild();
}

} // namespace tanara_qml
