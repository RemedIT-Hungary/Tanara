#include "MeetingTagsModel.h"

#include "AppContext.h"
#include "ShellFormat.h"
#include "TagDemoBackend.h"
#include "TagMatching.h"

#include <QVariantMap>

namespace tanara_qml {

MeetingTagsModel::MeetingTagsModel(QObject* parent) : QObject(parent)
{
    m_controller = AppContext::instance()->controllerObject();
    m_ownBackend = createTagBackend(m_controller, this);
    attach(m_ownBackend);
}

void MeetingTagsModel::setController(QObject* controller)
{
    if (m_controller == controller && m_backend == m_ownBackend) return;
    m_controller = controller;
    TagBackend* old = m_ownBackend;
    m_ownBackend = createTagBackend(controller, this);
    attach(m_ownBackend);
    if (old) old->deleteLater();
    emit controllerChanged();
}

void MeetingTagsModel::setBackend(TagBackend* backend)
{
    attach(backend ? backend : m_ownBackend);
}

void MeetingTagsModel::attach(TagBackend* backend)
{
    for (const auto& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();
    m_backend = backend;
    if (m_backend) {
        m_conns << connect(m_backend, &TagBackend::tagsChanged, this, [this] { reloadTags(); reloadSuggestions(); });
        m_conns << connect(m_backend, &TagBackend::meetingTagsChanged, this, [this](const QString& id) {
            if (id != effectiveMeetingId()) return;
            reloadTags();
            reloadSuggestions();
        });
        m_conns << connect(m_backend, &TagBackend::suggestionsChanged, this, [this](const QString& id) {
            if (id == effectiveMeetingId()) reloadSuggestions();
        });
        m_conns << connect(m_backend, &TagBackend::undoChanged, this, &MeetingTagsModel::undoChanged);
    }
    applyDemoState();
    reloadTags();
    reloadSuggestions();
    emit undoChanged();
}

QString MeetingTagsModel::effectiveMeetingId() const
{
    if (m_meetingId.isEmpty() && qobject_cast<TagDemoBackend*>(m_backend)) return TagDemoBackend::demoMeetingId();
    return m_meetingId;
}

void MeetingTagsModel::setMeetingId(const QString& id)
{
    if (m_meetingId == id) return;
    m_meetingId = id;
    emit meetingIdChanged();
    applyDemoState();
    reloadTags();
    reloadSuggestions();
    emit undoChanged();
}

void MeetingTagsModel::setDemoState(const QString& state)
{
    if (m_demoState == state) return;
    m_demoState = state;
    emit demoStateChanged();
    applyDemoState();
}

void MeetingTagsModel::applyDemoState()
{
    // Csak a saját (kitalált) backenden: valódi adatot a demó-állapot nem írhat felül.
    auto* demo = qobject_cast<TagDemoBackend*>(m_backend);
    if (!demo || m_demoState.isEmpty() || m_backend != m_ownBackend) return;
    demo->loadMeetingDemo(effectiveMeetingId(), m_demoState);
}

void MeetingTagsModel::reloadTags()
{
    QStringList ids, names;
    if (m_backend) {
        const QVector<TagItem> all = m_backend->tags();
        for (const QString& id : m_backend->tagsOf(effectiveMeetingId())) {
            for (const TagItem& t : all) {
                if (t.id != id) continue;
                ids << id;
                names << t.name;
                break;
            }
        }
    }
    if (ids == m_tagIds && names == m_tagNames) return;
    m_tagIds = ids;
    m_tagNames = names;
    emit tagsChanged();
}

bool MeetingTagsModel::applied(const TagSuggestionItem& s) const
{
    if (!s.tagId.isEmpty() && m_tagIds.contains(s.tagId)) return true;
    const QString k = tagmatch::key(s.name);
    for (const QString& n : m_tagNames)
        if (tagmatch::key(n) == k) return true;
    return false;
}

void MeetingTagsModel::reloadSuggestions()
{
    const QString meeting = effectiveMeetingId();
    const TagSuggestionState state = m_backend ? m_backend->suggestions(meeting) : TagSuggestionState();
    QVector<TagSuggestionItem> visible;
    for (const TagSuggestionItem& s : state.items) {
        if (applied(s)) continue;
        if (m_backend->isRejected(meeting, s.tagId.isEmpty() ? s.name : s.tagId)) continue;
        visible << s;
    }
    m_visible = visible;
    m_source = state.source;
    m_baseTagId = state.baseTagId;
    m_computing = state.computing;
    emit suggestionsChanged();
}

QVariantList MeetingTagsModel::tags() const
{
    QVariantList out;
    for (int i = 0; i < m_tagIds.size(); ++i)
        out << QVariantMap{{QStringLiteral("id"), m_tagIds[i]}, {QStringLiteral("name"), m_tagNames[i]}};
    return out;
}

QVariantList MeetingTagsModel::suggestions() const
{
    QVariantList out;
    for (const TagSuggestionItem& s : m_visible)
        out << QVariantMap{{QStringLiteral("id"), s.tagId},
                           {QStringLiteral("name"), s.name},
                           {QStringLiteral("isNew"), s.isNew},
                           {QStringLiteral("source"), s.source}};
    return out;
}

QString MeetingTagsModel::tagName(const QString& tagId) const
{
    const int i = m_tagIds.indexOf(tagId);
    if (i >= 0) return m_tagNames[i];
    if (m_backend)
        for (const TagItem& t : m_backend->tags())
            if (t.id == tagId) return t.name;
    return {};
}

QString MeetingTagsModel::suggestionLabel() const
{
    if (m_visible.isEmpty()) return {};
    if (m_source == QLatin1String("cooccur")) return tr("%1 mellé gyakran").arg(tagName(m_baseTagId));
    if (m_source == QLatin1String("llm")) return tr("Az összefoglaló alapján");
    return tr("Javasolt");
}

void MeetingTagsModel::finishStep()
{
    if (m_backend) emit toast(m_backend->undoLabel(), m_backend->canUndo());
}

QString MeetingTagsModel::add(const QString& nameOrId)
{
    if (!m_backend) return {};
    const QString meeting = effectiveMeetingId();
    QString name = tagName(nameOrId);
    if (name.isEmpty()) {
        // Név szerint: a meglévő címke a saját írásmódjával szerepel a lépés nevében.
        name = tagmatch::normalizeName(nameOrId);
        const QString k = tagmatch::key(name);
        for (const TagItem& t : m_backend->tags())
            if (tagmatch::key(t.name) == k) { name = t.name; break; }
    }
    if (name.isEmpty()) return {};
    const int before = int(m_tagIds.size());
    m_backend->beginGroup(tr("Címke hozzáadva: #%1").arg(name));
    const QString id = m_backend->addTag(meeting, nameOrId, TagAddSource::Manual);
    m_backend->endGroup();
    if (id.isEmpty()) return {};
    reloadTags();
    // Csak akkor kér együtt járó javaslatot, ha tényleg felkerült valami.
    if (m_tagIds.size() > before) m_backend->requestCooccur(meeting, id);
    return id;
}

void MeetingTagsModel::remove(const QString& tagId)
{
    if (!m_backend || !m_tagIds.contains(tagId)) return;
    m_backend->beginGroup(tr("Címke eltávolítva: #%1").arg(tagName(tagId)));
    m_backend->removeTag(effectiveMeetingId(), tagId);
    m_backend->endGroup();
    finishStep();
}

void MeetingTagsModel::accept(int index)
{
    if (!m_backend || index < 0 || index >= m_visible.size()) return;
    const TagSuggestionItem s = m_visible[index];
    m_backend->beginGroup(tr("Javaslat elfogadva: #%1").arg(s.name));
    m_backend->addTag(effectiveMeetingId(), s.isNew || s.tagId.isEmpty() ? s.name : s.tagId,
                      s.source == QLatin1String("llm") ? TagAddSource::Llm : TagAddSource::Suggestion);
    m_backend->endGroup();
    finishStep();
}

void MeetingTagsModel::reject(int index)
{
    if (!m_backend || index < 0 || index >= m_visible.size()) return;
    const TagSuggestionItem s = m_visible[index];
    m_backend->beginGroup(tr("Javaslat elutasítva: #%1").arg(s.name));
    m_backend->reject(effectiveMeetingId(), s);
    m_backend->endGroup();
    reloadSuggestions();
    finishStep();
}

void MeetingTagsModel::acceptAll()
{
    if (!m_backend || m_visible.isEmpty()) return;
    const QVector<TagSuggestionItem> all = m_visible;
    m_backend->beginGroup(all.size() == 1 ? tr("Javaslat elfogadva: #%1").arg(all.first().name)
                                          : tr("%1 javaslat elfogadva").arg(all.size()));
    for (const TagSuggestionItem& s : all)
        m_backend->addTag(effectiveMeetingId(), s.isNew || s.tagId.isEmpty() ? s.name : s.tagId,
                          s.source == QLatin1String("llm") ? TagAddSource::Llm : TagAddSource::Suggestion);
    m_backend->endGroup();
    finishStep();
}

void MeetingTagsModel::undo()
{
    if (m_backend) m_backend->undo();
}

void MeetingTagsModel::requestSuggestions()
{
    if (m_backend) m_backend->requestSuggestions(effectiveMeetingId());
}

QVariantList MeetingTagsModel::whyData() const
{
    QVariantList out;
    for (int i = 0; i < m_visible.size(); ++i) {
        const TagSuggestionItem& s = m_visible[i];
        QVariantList reasons;
        for (const TagReasonItem& r : s.reasons) {
            const QString label = r.kind == QLatin1String("participant") ? tr("Közös résztvevő")
                                : r.kind == QLatin1String("title")       ? tr("Hasonló cím")
                                                                         : tr("Közös kifejezések");
            reasons << QVariantMap{{QStringLiteral("kind"), r.kind},
                                   {QStringLiteral("values"), r.values},
                                   {QStringLiteral("label"), label},
                                   {QStringLiteral("text"), r.values.join(QStringLiteral(", "))}};
        }
        QVariantList meetings;
        for (const TagMeetingItem& m : s.similarMeetings)
            meetings << QVariantMap{{QStringLiteral("meetingId"), m.meetingId},
                                    {QStringLiteral("title"), m.title},
                                    {QStringLiteral("dateText"), fmt::shortDate(m.startedAt)}};
        out << QVariantMap{{QStringLiteral("index"), i},
                           {QStringLiteral("id"), s.tagId},
                           {QStringLiteral("name"), s.name},
                           {QStringLiteral("isNew"), s.isNew},
                           {QStringLiteral("source"), s.source},
                           {QStringLiteral("reasons"), reasons},
                           {QStringLiteral("similarMeetings"), meetings}};
    }
    return out;
}

} // namespace tanara_qml
