#pragma once
//
// UiServices: the facades the view models are built on, ONE set per mode. AppContext hands the
// set out: services() (the real ones of the process's controller, or the fictional ones when
// there is none / App.demo), servicesFor(controller), demoServices(). The view-model factories
// (QmlApp, *WindowHost, the QML default constructors) give a facade to the view model's
// constructor; the view models themselves never look at the controller.
//
// The set grows with the P15 sub-slices (library, transcript, summary, settings ...); the people, tag
// and participants side is in since sub-slice 5.
//
#include <QObject>
#include <QString>

#include <functional>

namespace tanara::recording { class IRecorderFacade; }
namespace tanara::library { class ILibraryFacade; class IImportFacade; }
namespace tanara::editing { class IShellFacade; }
namespace tanara::library { class IOverviewFacade; }
namespace tanara::library { class IPeopleFacade; class TagBackend; }
namespace tanara::editing { class IParticipantsFacade; }
namespace tanara::processing { class ISummaryFacade; }
namespace tanara::editing { class ITranscriptEditorFacade; }
namespace tanara::processing { class ITranscribeStepFacade; }
// --- P15-6a ---
namespace tanara::app { class ISettingsFacade; }
namespace tanara::processing { class IVoiceModelsFacade; }
namespace tanara::library { class IMeetingNoteFacade; class ITracksFacade; }
// --- end P15-6a ---

namespace tanara_qml {

class TrackActivitySource;

struct UiServices {
    // The recorder (devices, levels, recording, the finished meeting's notes, tags).
    tanara::recording::IRecorderFacade* recorder = nullptr;
    // The speech-activity frames of the tracks. A factory: the demo source depends on the
    // asking model's scenario (its demoState); the real source is shared.
    std::function<TrackActivitySource*(const QString& scenario, QObject* parent)> trackActivity;
    // The library column: the list, search and filters, "Waiting for you", the multi-select panel.
    tanara::library::ILibraryFacade* library = nullptr;
    // The shell: the action surface, gating, shared undo, the selected meeting's header and task bar.
    tanara::editing::IShellFacade* shell = nullptr;
    // The "Import audio file" window.
    tanara::library::IImportFacade* import = nullptr;
    // The Overview tab. A factory: the demo facade serves one design state (its scenario) and keeps
    // its edits, so every asking model gets its own; the real one is shared.
    std::function<tanara::library::IOverviewFacade*(const QString& scenario, QObject* parent)> overview;
    // The tag side (pool, a meeting's tags, suggestions, people tags, undo). A factory: every view
    // model gets a backend of its own (the demo one keeps its in-memory state per asking model; the
    // real one keeps the suggestion state of its view).
    std::function<tanara::library::TagBackend*(QObject* parent)> tagBackend;
    // The People window's people side. A factory: the real facade is shared (parent ignored), the
    // demo one is the asking view model's own (its design state).
    std::function<tanara::library::IPeopleFacade*(QObject* parent)> people;
    // The participants side ("Who was there?", the speaker -> track chips, the pickers): the same.
    std::function<tanara::editing::IParticipantsFacade*(QObject* parent)> participants;
    // The Summary tab's facade. A factory: the demo facade is a fixture of the asking view model's
    // demoState (it lives under `parent`); the real facade is shared (one per controller).
    std::function<tanara::processing::ISummaryFacade*(const QString& scenario, QObject* parent)> summary;
    // The Transcript tab (editor sessions, the pickers' people, the tag side). A factory: the demo
    // facade builds its own fictional meeting of the given variant (it lives under `parent`); the
    // real one is shared.
    std::function<tanara::editing::ITranscriptEditorFacade*(const QString& variant, QObject* parent)> transcriptEditor;
    // The pre-transcript view (the transcription step, its run and error). A factory: the demo facade
    // serves one design state (its scenario; it lives under `parent`); the real one is shared.
    std::function<tanara::processing::ITranscribeStepFacade*(const QString& scenario, QObject* parent)> transcribeStep;
    // --- P15-6a: settings, onboarding, voice models, tracks, meeting note, UI state ---
    // The Settings window's facade. A factory: the demo facade serves one design state (its scenario,
    // e.g. "B04"; it lives under `parent`); the real one is shared.
    std::function<tanara::app::ISettingsFacade*(const QString& scenario, QObject* parent)> settings;
    // The "Getting started" window's: the same surface, the demo facade with the onboarding fixture.
    std::function<tanara::app::ISettingsFacade*(const QString& scenario, QObject* parent)> onboarding;
    // The voice-model list (Settings > Speakers, Getting started, the People window's note): the same.
    std::function<tanara::processing::IVoiceModelsFacade*(const QString& scenario, QObject* parent)> voiceModels;
    // The Tracks tab and the player: the same (the demo facade keeps its design state's track list).
    std::function<tanara::library::ITracksFacade*(const QString& scenario, QObject* parent)> tracks;
    // The meeting note editor's facade (shared); nullptr in the demo, where the owner view model fills
    // the sample content.
    tanara::library::IMeetingNoteFacade* meetingNotes = nullptr;
    // The main window's UI state file (ui-state.json); empty in the demo (nothing is written).
    QString uiStateFile;
    // --- end P15-6a ---
};

} // namespace tanara_qml
