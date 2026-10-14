#include "tanara/editing/people/PeopleEditingAdapter.h"

#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/TranscriptRepository.h"

#include <QFile>

#include <algorithm>

namespace tanara::editing {

namespace {

// The person's speaker keys in an editor session.
QStringList speakerKeysOf(const SpeakerEditor& ed, const PersonId& person)
{
    QStringList keys;
    for (const EditorSpeaker& s : ed.speakers())
        if (!s.anonymous && s.personId == person) keys << s.key;
    return keys;
}

} // namespace

PeopleEditingAdapter::PeopleEditingAdapter(const Deps& deps) : m_deps(deps) {}

PeopleEditingAdapter::~PeopleEditingAdapter() = default;

PeopleEditingAdapter::EmbedderFactory PeopleEditingAdapter::currentFactory() const
{
    if (m_factoryInjected) return m_injected;
    return m_deps.embedderFactory ? m_deps.embedderFactory() : EmbedderFactory();
}

void PeopleEditingAdapter::setEmbedderFactory(EmbedderFactory factory)
{
    m_injected = std::move(factory);
    m_factoryInjected = true;
}

bool PeopleEditingAdapter::canCreate() const { return bool(currentFactory()); }

qint64 PeopleEditingAdapter::bestUsableMs(const QString& meetingId, const PersonId& person)
{
    // The existing editor logic tells whether there is a sure (not doubtful), long line. The plan's editor runs
    // with the models in use but without the embedder (with an injected factory the editor's default stays).
    SpeakerEditor ed(m_deps.store, m_deps.people, m_deps.voiceprints, meetingId);
    if (!m_factoryInjected && m_deps.editorModelIds) ed.setVoiceModelIds(m_deps.editorModelIds());
    qint64 best = 0;
    for (const QString& key : speakerKeysOf(ed, person)) {
        const VoiceprintMaterial material = ed.voiceprintMaterial(key);
        if (material.sufficient) best = std::max(best, material.usableMs);
    }
    return best;
}

VoiceprintResult PeopleEditingAdapter::createVoiceprint(const QString& meetingId, const PersonId& person,
                                                        bool* noMaterial)
{
    if (noMaterial) *noMaterial = false;
    const EmbedderFactory factory = currentFactory();
    // Its own, temporary editor session: with the models in use (with an injected factory the editor's
    // default model list stays) and the embedder.
    SpeakerEditor ed(m_deps.store, m_deps.people, m_deps.voiceprints, meetingId);
    if (!m_factoryInjected && m_deps.editorModelIds) ed.setVoiceModelIds(m_deps.editorModelIds());
    if (factory) ed.setEmbedderFactory(factory);
    QString bestKey;
    qint64 bestMs = 0;
    for (const QString& key : speakerKeysOf(ed, person)) {
        const VoiceprintMaterial material = ed.voiceprintMaterial(key);
        if (material.sufficient && material.usableMs > bestMs) {
            bestMs = material.usableMs;
            bestKey = key;
        }
    }
    if (bestKey.isEmpty()) {
        if (noMaterial) *noMaterial = true;
        return {};
    }
    return ed.createVoiceprint(bestKey);
}

QStringList PeopleEditingAdapter::markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys)
{
    return speakeredit::markSummaryStaleKeys(m, speakerKeys);
}

bool PeopleEditingAdapter::unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys)
{
    return speakeredit::unmarkSummaryStale(meetingFolder, speakerKeys);
}

bool PeopleEditingAdapter::isParticipantKey(const QString& key) const { return speakeredit::isParticipantKey(key); }

bool PeopleEditingAdapter::overlayRefersTo(const QString& meetingFolder, const PersonId& person) const
{
    if (meetingFolder.isEmpty() || !QFile::exists(speakeredit::overlayPath(meetingFolder))) return false;
    const SpeakerOverlay ov = speakeredit::loadOverlay(meetingFolder);
    for (const OverlayParticipant& p : ov.participants) if (p.personId == person) return true;
    for (const OverlayIdentification& i : ov.identified) if (i.personId == person) return true;
    return false;
}

bool PeopleEditingAdapter::removePersonFromOverlay(const QString& meetingFolder, const PersonId& person)
{
    return speakeredit::removePersonFromOverlay(meetingFolder, person);
}

OverlayPersonRefs PeopleEditingAdapter::replacePersonInOverlay(const QString& meetingFolder, const PersonId& from,
                                                               const PersonId& to)
{
    return speakeredit::replacePersonInOverlay(meetingFolder, from, to);
}

bool PeopleEditingAdapter::restorePersonInOverlay(const QString& meetingFolder, const OverlayPersonRefs& refs,
                                                  const PersonId& current, const PersonId& original)
{
    return speakeredit::restorePersonInOverlay(meetingFolder, refs, current, original);
}

void PeopleEditingAdapter::rewriteTranscriptNames(const Meeting& m, const PersonNames& names)
{
    MergedTranscript merged = library::TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return;
    speakeredit::applyResolvedSpeakers(merged, m, names);
    library::TranscriptRepository::writeMarkdown(m.folder, merged.renderMarkdown());
}

} // namespace tanara::editing

// library::PeopleService's SideReport overload is defined here: SideReport and the learning rule
// (sides::learnedDefaults) are the editing module's; library only gets the result.
namespace tanara::library {

int PeopleService::learnDefaultSides(const editing::SideReport& report)
{
    QHash<PersonId, QString> learned;
    const QHash<PersonId, editing::Side> sides = editing::sides::learnedDefaults(report);
    for (auto it = sides.cbegin(); it != sides.cend(); ++it) learned.insert(it.key(), editing::sideName(it.value()));
    return learnDefaultSides(learned);
}

} // namespace tanara::library
