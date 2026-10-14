#pragma once
//
// Test-only owner of one speaker-editor session and its voice analysis, paired the way
// editing::EditorSessions pairs them in the app (the RecheckService is the session's child) and with the
// app's two defaults (app::installEditorDefaults: the tracks.activity.bin cache reader, the voice models'
// display names). Not a facade: the tests call the two parts directly -- the session through -> / * /
// session(), the voice analysis through recheck().
#include "tanara/app/EditorDefaults.h"
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"

#include <memory>

namespace test_support {

class EditSessionHarness {
public:
    EditSessionHarness(tanara::library::MeetingRepository* store, tanara::library::PeopleStore* people,
                       tanara::library::VoiceprintStore* voiceprints, const QString& meetingId)
        : m_session(std::make_unique<tanara::editing::SpeakerEditSession>(store, people, voiceprints, meetingId))
        , m_recheck(new tanara::editing::RecheckService(m_session.get(), m_session.get()))
    {
        tanara::app::installEditorDefaults(*m_session, *m_recheck);
    }

    tanara::editing::SpeakerEditSession* session() const { return m_session.get(); }
    tanara::editing::RecheckService* recheck() const { return m_recheck; }
    tanara::editing::SpeakerEditSession* get() const { return m_session.get(); }
    tanara::editing::SpeakerEditSession* operator->() const { return m_session.get(); }
    tanara::editing::SpeakerEditSession& operator*() const { return *m_session; }

    // Both parts take the embedder factory: the session fills a manual voiceprint's missing vectors with
    // it, the voice analysis runs the background embedding with it (as app::Services configures them).
    void setEmbedderFactory(tanara::providers::UtteranceEmbedderFactory factory)
    {
        m_session->setEmbedderFactory(factory);
        m_recheck->setEmbedderFactory(std::move(factory));
    }

    // Destroys the session (and its voice analysis) now, as closing the editor does.
    void reset()
    {
        m_session.reset();
        m_recheck = nullptr;
    }

private:
    std::unique_ptr<tanara::editing::SpeakerEditSession> m_session;
    tanara::editing::RecheckService* m_recheck = nullptr;   // the session's child
};

} // namespace test_support
