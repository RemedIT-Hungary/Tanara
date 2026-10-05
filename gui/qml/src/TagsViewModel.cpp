#include "TagsViewModel.h"

#include "AppContext.h"
#include "ShellFormat.h"
#include "TagDemoBackend.h"
#include "TagMatching.h"

#include "tanara/library/TextFold.h"

#include <QCoreApplication>

#include <algorithm>

namespace tanara_qml {

namespace {

QString monogramOf(const QString& name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString& w : words) {
        if (!w.at(0).isLetterOrNumber()) continue;
        out += w.at(0).toUpper();
        if (out.size() == 2) break;
    }
    if (out.size() == 1 && words.size() == 1 && words.first().size() > 1) out += words.first().at(1).toUpper();
    return out.isEmpty() ? QStringLiteral("?") : out;
}

// „Hasonló név”: ugyanaz / közeli kulcs, vagy az egyik kulcs a másik része („Nordvik AS”).
bool similarName(const QString& a, const QString& b)
{
    const QString ka = tagmatch::key(a), kb = tagmatch::key(b);
    if (ka.size() >= 3 && kb.size() >= 3 && (ka.contains(kb) || kb.contains(ka))) return true;
    return tagmatch::nearDuplicate(a, b) || (std::min(ka.size(), kb.size()) >= 5 && tagmatch::editDistance(ka, kb) <= 2);
}

} // namespace

QString tagArticle(const QString& name)
{
    const QString folded = tagmatch::foldName(name);
    for (const QChar c : folded) {
        if (!c.isLetterOrNumber()) continue;
        const bool vowel = QStringLiteral("aeiou15").contains(c);
        return vowel ? QCoreApplication::translate("TagsViewModel", "az", "névelő magánhangzó előtt")
                     : QCoreApplication::translate("TagsViewModel", "a", "névelő mássalhangzó előtt");
    }
    return QCoreApplication::translate("TagsViewModel", "a", "névelő mássalhangzó előtt");
}

TagsViewModel::TagsViewModel(QObject* parent) : QObject(parent)
{
    m_controller = AppContext::instance()->controllerObject();
    m_ownBackend = createTagBackend(m_controller, this);
    attach(m_ownBackend);
}

void TagsViewModel::setController(QObject* controller)
{
    if (m_controller == controller && m_backend == m_ownBackend) return;
    m_controller = controller;
    TagBackend* old = m_ownBackend;
    m_ownBackend = createTagBackend(controller, this);
    attach(m_ownBackend);
    if (old) old->deleteLater();
    emit controllerChanged();
}

void TagsViewModel::setBackend(TagBackend* backend)
{
    attach(backend ? backend : m_ownBackend);
}

void TagsViewModel::attach(TagBackend* backend)
{
    for (const auto& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();
    m_backend = backend;
    m_selected.clear();
    if (m_backend) {
        m_conns << connect(m_backend, &TagBackend::tagsChanged, this, &TagsViewModel::rebuild);
        m_conns << connect(m_backend, &TagBackend::undoChanged, this, &TagsViewModel::undoChanged);
    }
    rebuild();
    emit undoChanged();
}

void TagsViewModel::setDemoState(const QString& state)
{
    if (m_demoState == state) return;
    m_demoState = state;
    emit demoStateChanged();
    auto* demo = qobject_cast<TagDemoBackend*>(m_backend);
    if (!demo || m_backend != m_ownBackend) return;
    if (state == QLatin1String("T13")) {
        demo->clearAll();
        return;
    }
    m_filter.clear();
    emit filterChanged();
    rebuild();
    setSelectedId(QStringLiteral("t-nordvik"));
}

const TagItem* TagsViewModel::find(const QString& id) const
{
    for (const TagItem& t : m_all)
        if (t.id == id) return &t;
    return nullptr;
}

QString TagsViewModel::tagName(const QString& id) const
{
    const TagItem* t = find(id);
    return t ? t->name : QString();
}

void TagsViewModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void TagsViewModel::setSort(const QString& sort)
{
    if (m_sort == sort) return;
    if (sort != QLatin1String("recent") && sort != QLatin1String("alpha") && sort != QLatin1String("count")) return;
    m_sort = sort;
    emit sortChanged();
    rebuild();
}

QString TagsViewModel::sortLabel() const
{
    if (m_sort == QLatin1String("alpha")) return tr("ABC");
    if (m_sort == QLatin1String("count")) return tr("Leggyakoribb");
    return tr("Legutóbb használt");
}

QString TagsViewModel::countText() const
{
    return searching() ? tr("%1 / %2 címke").arg(count()).arg(totalCount()) : tr("%1 címke").arg(totalCount());
}

void TagsViewModel::rebuild()
{
    m_all = m_backend ? m_backend->tags() : QVector<TagItem>();
    QVector<TagItem> sorted = m_all;
    if (m_sort == QLatin1String("alpha")) {
        std::stable_sort(sorted.begin(), sorted.end(), [](const TagItem& a, const TagItem& b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
    } else if (m_sort == QLatin1String("count")) {
        std::stable_sort(sorted.begin(), sorted.end(), [](const TagItem& a, const TagItem& b) {
            if (a.meetingCount != b.meetingCount) return a.meetingCount > b.meetingCount;
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
    } else {
        std::stable_sort(sorted.begin(), sorted.end(), [](const TagItem& a, const TagItem& b) {
            return a.lastUsedAt > b.lastUsedAt;
        });
    }

    const QString needle = tanara::textfold::foldQuery(m_filter);
    QVector<TagListRow> rows;
    for (const TagItem& t : std::as_const(sorted)) {
        TagListRow r{t.id, t.name, tr("%1 megbeszélés · %2").arg(t.meetingCount).arg(fmt::shortDate(t.lastUsedAt)),
                     t.name, QString(), QString()};
        if (!needle.isEmpty()) {
            const QString nfc = tanara::textfold::normalize(t.name);
            const int at = tanara::textfold::fold(nfc).indexOf(needle);
            if (at < 0) continue;
            if (nfc == t.name) {
                r.before = t.name.left(at);
                r.match = t.name.mid(at, needle.size());
                r.after = t.name.mid(at + needle.size());
            }
        }
        rows << r;
    }
    m_list.setRows(rows);
    emit listChanged();

    // A kijelölés: ha eltűnt (törlés, összevonás), az első sor.
    if (!find(m_selected)) {
        m_selected = rows.isEmpty() ? QString() : rows.first().id;
        emit selectionChanged();
    }
    refreshDetail();
}

void TagsViewModel::setSelectedId(const QString& id)
{
    if (m_selected == id || (!id.isEmpty() && !find(id))) return;
    m_selected = id;
    emit selectionChanged();
    refreshDetail();
}

void TagsViewModel::refreshDetail()
{
    QVariantMap d;
    const TagItem* t = find(m_selected);
    if (t && m_backend) {
        const TagProfileItem p = m_backend->profile(t->id);
        d.insert(QStringLiteral("id"), t->id);
        d.insert(QStringLiteral("name"), t->name);
        d.insert(QStringLiteral("meetingCount"), t->meetingCount);
        d.insert(QStringLiteral("meta"), tr("%1 megbeszélés · először: %2 · utoljára: %3")
                                             .arg(t->meetingCount)
                                             .arg(fmt::shortDate(t->firstUsedAt), fmt::shortDate(t->lastUsedAt)));
        QVariantList participants;
        for (const auto& [name, n] : p.participants)
            participants << QVariantMap{{QStringLiteral("name"), name},
                                        {QStringLiteral("monogram"), monogramOf(name)},
                                        {QStringLiteral("countText"), QStringLiteral("%1 / %2").arg(n).arg(t->meetingCount)}};
        d.insert(QStringLiteral("participants"), participants);
        d.insert(QStringLiteral("terms"), p.terms);
        QVariantList co;
        for (const auto& [id, n] : p.cooccurring) {
            // Az egyszeri együttállás nem „gyakran együtt”.
            if (n < 2 || co.size() >= 6) continue;
            co << QVariantMap{{QStringLiteral("id"), id},
                              {QStringLiteral("name"), tagName(id)},
                              {QStringLiteral("countText"), QStringLiteral("%1×").arg(n)}};
        }
        d.insert(QStringLiteral("cooccurring"), co);
        QVariantList meetings;
        for (const TagMeetingItem& m : p.meetings)
            meetings << QVariantMap{{QStringLiteral("meetingId"), m.meetingId},
                                    {QStringLiteral("title"), m.title},
                                    {QStringLiteral("dateText"), m.startedAt.date().toString(Qt::ISODate)},
                                    {QStringLiteral("durationText"), fmt::shortDuration(m.durationMs)}};
        d.insert(QStringLiteral("meetings"), meetings);
        d.insert(QStringLiteral("meetingsMeta"), tr("%1 · legújabb elöl").arg(t->meetingCount));
    }
    if (d == m_detail) return;
    m_detail = d;
    emit detailChanged();
}

void TagsViewModel::step(const QString& label)
{
    m_toastText = m_backend && m_backend->canUndo() ? m_backend->undoLabel() : label;
    emit toast(m_toastText, m_backend && m_backend->canUndo());
}

QString TagsViewModel::rename(const QString& id, const QString& name)
{
    const TagItem* t = find(id);
    if (!t || !m_backend) return tr("A címke már nem létezik.");
    const QString clean = tagmatch::normalizeName(name);
    if (clean.isEmpty()) return tr("Adj meg egy nevet.");
    if (clean == t->name) return {};
    const QString old = t->name;
    m_backend->beginGroup(tr("Átnevezve: #%1 → #%2").arg(old, clean));
    const bool ok = m_backend->rename(id, clean);
    m_backend->endGroup();
    if (!ok) return tr("Már van „%1” nevű címke. Ha ugyanazt jelentik, vond össze őket.").arg(clean);
    step(tr("Átnevezve: #%1 → #%2").arg(old, clean));
    return {};
}

QVariantList TagsViewModel::mergeCandidates(const QString& id) const
{
    QVariantList out;
    const TagItem* self = find(id);
    if (!self || !m_backend) return out;
    QStringList listed;
    // 1. Hasonló nevűek (legfeljebb 3), gyakoriság szerint.
    QVector<const TagItem*> similar;
    for (const TagItem& t : m_all)
        if (t.id != id && similarName(self->name, t.name)) similar << &t;
    std::stable_sort(similar.begin(), similar.end(), [](const TagItem* a, const TagItem* b) {
        return a->meetingCount > b->meetingCount;
    });
    for (const TagItem* t : std::as_const(similar)) {
        if (listed.size() >= 3) break;
        listed << t->id;
        out << QVariantMap{{QStringLiteral("id"), t->id},
                           {QStringLiteral("name"), t->name},
                           {QStringLiteral("meta"), tr("%1 megbeszélés · hasonló név").arg(t->meetingCount)}};
    }
    // 2. Gyakran együtt járók: a címke megbeszéléseinek legalább 40 %-án.
    const TagProfileItem p = m_backend->profile(id);
    for (const auto& [otherId, n] : p.cooccurring) {
        const TagItem* t = find(otherId);
        if (!t || listed.contains(otherId) || n < 2 || n * 5 < self->meetingCount * 2) continue;
        listed << otherId;
        out << QVariantMap{{QStringLiteral("id"), otherId},
                           {QStringLiteral("name"), t->name},
                           {QStringLiteral("meta"), tr("%1 megbeszélés · %2× szerepeltek együtt").arg(t->meetingCount).arg(n)}};
    }
    return out;
}

QString TagsViewModel::mergeResultText(const QString& fromId, const QString& keepId) const
{
    const TagItem* from = find(fromId);
    const TagItem* keep = find(keepId);
    if (!from || !keep || !m_backend) return {};
    int common = 0;
    for (const auto& [id, n] : m_backend->profile(keepId).cooccurring)
        if (id == fromId) common = n;
    const int total = keep->meetingCount + from->meetingCount - common;
    return tr("Eredmény: <b>%1 megbeszélés</b> (%2 közös). A „%3” név megszűnik; ha valaki beírja, a mező %4 #%5 címkét ajánlja.")
        .arg(total)
        .arg(common)
        .arg(from->name.toHtmlEscaped(), tagArticle(keep->name), keep->name.toHtmlEscaped());
}

QString TagsViewModel::merge(const QString& fromId, const QString& keepId)
{
    const TagItem* from = find(fromId);
    const TagItem* keep = find(keepId);
    if (!from || !keep || !m_backend) return tr("A címke már nem létezik.");
    if (fromId == keepId) return tr("Válassz egy másik címkét.");
    const QString label = tr("Összevonva: #%1 → #%2").arg(from->name, keep->name);
    m_backend->beginGroup(label);
    m_backend->merge(fromId, keepId);
    m_backend->endGroup();
    setSelectedId(keepId);
    step(label);
    return {};
}

QString TagsViewModel::deleteTitle(const QString& id) const
{
    const QString name = tagName(id);
    return tr("Törlöd %1 #%2 címkét?").arg(tagArticle(name), name);
}

QString TagsViewModel::deleteText(const QString& id) const
{
    const TagItem* t = find(id);
    if (!t) return {};
    return tr("%1 megbeszélésről lekerül. Maguk a megbeszélések, átiratok és összefoglalók megmaradnak. "
              "A címke profilja és az együtt járási adatai törlődnek.").arg(t->meetingCount);
}

void TagsViewModel::remove(const QString& id)
{
    const TagItem* t = find(id);
    if (!t || !m_backend) return;
    const QString label = tr("Címke törölve: #%1").arg(t->name);
    m_backend->beginGroup(label);
    m_backend->remove(id);
    m_backend->endGroup();
    step(label);
}

void TagsViewModel::undo()
{
    if (m_backend) m_backend->undo();
}

} // namespace tanara_qml
