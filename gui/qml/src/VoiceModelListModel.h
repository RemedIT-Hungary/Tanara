#pragma once
//
// VoiceModelListModel: the list of speaker-recognition voice models (Settings > Speakers,
// Getting started): download, enabling, the voiceprint backfill state.
//
//  - rows: the VoiceModelRegistry's built-in descriptors (name, one sentence, license, source,
//    size, dimension) + state: missing | downloading | verifying | downloaded | bundled | error | offline
//    | mismatch (the SHA-256 differs from the pinned one: warning + "Mégis használom") | unverified
//    (the user accepted that file; usable, flagged);
//  - verification: after a download and when the page opens (refresh), the core's
//    VoiceModelDownloader::verify (file size); meanwhile "verifying", on a mismatch "error" +
//    corrupt (Retry: replaces it);
//  - delete: deletes the DOWNLOADED file (<metaDir>/models) (IVoiceModelsFacade::deleteModelFile),
//    never touches a bundled one; disabled during a running download / verification;
//  - rescan: after a manually copied model file (IVoiceModelsFacade::rescan);
//  - download: the core's VoiceModelDownloader (immediate, NOT a draft); after a successful
//    download the model is enabled in the draft (takes effect on Save);
//  - enabling: into the Settings draft (setDraftAccess: provided by SettingsViewModel); a
//    standalone instance without a draft writes the core's setting immediately;
//  - backfill: the facade's backfillStarted / Progress / Finished (real "k / N"), the
//    skipped-samples list from IVoiceModelsFacade::backfillSkips (person, sample, reason: one
//    item per sample, however many models are missing).
//
// It works on a tanara::processing::IVoiceModelsFacade: from QML the one of AppContext::services();
// inside Settings the view model's (useServices). On the fictional facade (tanara_demo: demo,
// --qml-shot) the rows show a design state; demoState: B08 | B08download | B08none | B08fallback |
// B08error | B08offline | B08backfill | B08partial | B08bundled | B08verify | B08delete | B08mismatch | B08unverified.
//
#include "tanara/processing/voiceid/IVoiceModelsFacade.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace tanara {
namespace processing { class VoiceModelDownloader; }
}

namespace tanara_qml {

struct UiServices;

class VoiceModelListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(int count READ count NOTIFY summaryChanged)
    // Whether there is at least one usable (downloaded or bundled) model.
    Q_PROPERTY(bool anyAvailable READ anyAvailable NOTIFY summaryChanged)
    // Number of enabled AND existing models (per the draft); 0: audio analysis is off.
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY summaryChanged)
    // The recommended model (WeSpeaker): the onboarding and the empty state's CTA.
    // True if there is no usable enabled model and the CAM++ fallback runs instead.
    Q_PROPERTY(bool fallbackActive READ fallbackActive NOTIFY summaryChanged)
    Q_PROPERTY(QString defaultState READ defaultState NOTIFY summaryChanged)
    Q_PROPERTY(QString defaultSizeText READ defaultSizeText CONSTANT)
    Q_PROPERTY(QString defaultProgressText READ defaultProgressText NOTIFY summaryChanged)
    Q_PROPERTY(double defaultProgress READ defaultProgress NOTIFY summaryChanged)   // -1: unknown
    Q_PROPERTY(QString defaultErrorText READ defaultErrorText NOTIFY summaryChanged)

    // ---- backfill ----
    // "" (nothing to show) | "running" | "done" | "partial" (a sample that cannot be backfilled remains)
    Q_PROPERTY(QString backfillState READ backfillState NOTIFY backfillChanged)
    Q_PROPERTY(int backfillTotal READ backfillTotal NOTIFY backfillChanged)
    Q_PROPERTY(int backfillDone READ backfillDone NOTIFY backfillChanged)
    Q_PROPERTY(int backfillSkipped READ backfillSkipped NOTIFY backfillChanged)
    Q_PROPERTY(double backfillProgress READ backfillProgress NOTIFY backfillChanged)   // -1: indeterminate
    Q_PROPERTY(QString backfillText READ backfillText NOTIFY backfillChanged)
    // The skipped samples: [{ person, sample, reason, models }] (sorted by person).
    Q_PROPERTY(QVariantList backfillSkips READ backfillSkips NOTIFY backfillChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        BlurbRole,
        LicenseRole,
        LicenseNoteRole,
        SourceUrlRole,
        SourceTextRole,
        SizeTextRole,
        DimTextRole,
        StateRole,          // missing | downloading | verifying | downloaded | bundled | error | offline
        ProgressRole,       // 0..1 (while downloading), -1 if unknown
        ProgressTextRole,   // "12 / 27 MB"
        ErrorTextRole,
        EnabledRole,        // enabled per the draft
        CanEnableRole,      // the file is present (downloaded / bundled)
        RecommendedRole,
        NotRecommendedRole,
        CorruptRole,        // the existing file's size differs from the expected ("error (size mismatch)")
        HasFileRole,        // there is a DOWNLOADED file (<metaDir>/models); the menu's "Delete" needs it
        CanDeleteRole,      // can be deleted now (a downloaded file exists, not downloading / verifying)
    };
    Q_ENUM(Role)

    explicit VoiceModelListModel(QObject* parent = nullptr);
    ~VoiceModelListModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // An explicit facade (tests); from now on the process's services no longer apply.
    void setFacade(tanara::processing::IVoiceModelsFacade* facade);
    tanara::processing::IVoiceModelsFacade* facade() const { return m_facade; }
    // The owner's (SettingsViewModel's) set: the facade of `services` in the given design state.
    void useServices(const UiServices& services, const QString& scenario = QString());
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    int count() const { return m_rows.size(); }
    bool anyAvailable() const;
    int activeCount() const;
    bool offline() const { return m_offline; }
    bool fallbackActive() const;
    QString defaultState() const;
    QString defaultSizeText() const;
    QString defaultProgressText() const;
    double defaultProgress() const;
    QString defaultErrorText() const;

    QString backfillState() const { return m_bfState; }
    int backfillTotal() const { return m_bfTotal; }
    int backfillDone() const { return m_bfDone; }
    int backfillSkipped() const { return m_bfSkipped; }
    double backfillProgress() const;
    QString backfillText() const;
    QVariantList backfillSkips() const { return m_bfSkips; }

    // ---- actions ----
    // Starts a download (does not start offline: the state becomes "offline").
    Q_INVOKABLE void download(const QString& id);
    // Retry: after an error; also replaces a corrupt (wrong-size) file.
    Q_INVOKABLE void retry(const QString& id);
    Q_INVOKABLE void cancel(const QString& id);
    // Enabling in the draft (only for an existing model).
    Q_INVOKABLE void setEnabled(const QString& id, bool on);
    // Opens the source page (license, authors): openUrlRequested.
    Q_INVOKABLE void openSource(const QString& id);
    // Re-reads the state (files, network, backfill) and verifies existing files
    // (this also runs when the page opens).
    Q_INVOKABLE void refresh();
    // Verifies one model file (size): "verifying" meanwhile.
    Q_INVOKABLE void verify(const QString& id);
    // "Mégis használom": the user overrides a SHA-256 mismatch. A parked download moves into place (and is
    // enabled like after a normal download); the model is then marked "unverified". Empty: succeeded;
    // otherwise a human error message.
    Q_INVOKABLE QString acceptMismatch(const QString& id);
    // Deletes the downloaded model file. Empty: succeeded; otherwise a human error message.
    Q_INVOKABLE QString deleteModel(const QString& id);
    // After a manually copied model file: the core recomputes the used models, the list refreshes.
    Q_INVOKABLE void rescan();

    // ---- for the SettingsViewModel ----
    // Draft access: get -> enabled ids, set -> new list (the vm computes the change).
    void setDraftAccess(std::function<QStringList()> get, std::function<void(const QStringList&)> set);
    // The draft changed from outside (load, discard, save).
    void notifyDraftChanged();

    // ---- teszt-varratok ----
    tanara::processing::VoiceModelDownloader* downloader() const { return m_downloader; }
    // Replaces the descriptors (e.g. with file:// download URLs) and the folders.
    void setSpecsForTest(const QVector<tanara::processing::VoiceModelSpec>& specs);
    void setDirsForTest(const QString& metaDir, const QString& appDir);

signals:
    void demoStateChanged();
    void summaryChanged();
    void backfillChanged();
    void openUrlRequested(const QString& url);
    // A download finished (ok: succeeded).
    void downloadFinished(const QString& id, bool ok);
    // A verification finished (ok: the file is usable).
    void verifyFinished(const QString& id, bool ok);

private:
    struct Row {
        tanara::processing::VoiceModelSpec spec;
        QString state = QStringLiteral("missing");
        qint64 received = 0, total = 0;
        QString errorText;
        bool corrupt = false;       // the existing file's size differs from the expected
        bool hasFile = false;       // there is a downloaded file (<metaDir>/models) or a parked one
        bool parked = false;        // a SHA-mismatching download waits aside as <file>.mismatch (not in use)
        QString trust;              // the last SHA-256 verification: "" (ok / unknown) | "mismatch" | "unverified"
    };

    void resolveServices();
    void attach();
    void applyScenario();
    bool live() const;
    void rebuild();
    void refreshFiles();
    void updateFileState(Row& r) const;
    int indexOf(const QString& id) const;
    void emitRow(int i);
    QString metaDir() const;
    QString appDir() const;
    QStringList enabledIds() const;
    void writeEnabled(const QStringList& ids);
    void startDownload(const QString& id, bool overwrite);
    void onProgress(const QString& id, qint64 received, qint64 total);
    void onFinished(const QString& id, bool ok, const QString& error);
    void onVerified(const QString& id, int result);
    void verifyAllFiles();
    QString userPath(const tanara::processing::VoiceModelSpec& spec) const;
    // Watching the backfill (the core's signals).
    void checkBackfill();
    void onBackfillStarted(int total);
    void onBackfillProgress(int done, int total);
    void onBackfillFinished(int added, int skipped);
    void loadSkips();
    void setBackfill(const QString& state, int total, int done, int skipped);

    QPointer<tanara::processing::IVoiceModelsFacade> m_facade;
    bool m_explicitFacade = false;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;
    tanara::processing::VoiceModelDownloader* m_downloader = nullptr;

    QVector<Row> m_rows;
    QVector<tanara::processing::VoiceModelSpec> m_specOverride;
    QString m_metaOverride, m_appOverride;
    bool m_dirsOverridden = false;
    std::function<QStringList()> m_getDraft;
    std::function<void(const QStringList&)> m_setDraft;
    bool m_offline = false;

    QString m_bfState;
    int m_bfTotal = 0, m_bfDone = 0, m_bfSkipped = 0;
    bool m_bfSeenRunning = false;
    QVariantList m_bfSkips;
};

// Indicates details in the People window (PeopleDetailPane): how many of the selected
// person's samples could not be backfilled in the latest voiceprint backfill ("1 sample
// cannot be backfilled: missing recording"). It takes the voice-model facade of
// AppContext::services(); the fictional one shows a skipped sample in the "samplesModels" state.
//   BackfillSkipNote { person: vm.selectedName; demoState: vm.demoState }
class BackfillSkipNote : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString person READ person WRITE setPerson NOTIFY personChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // The skipped samples' details (tooltip): "Meeting · 12:03-12:06 - missing recording".
    Q_PROPERTY(QString detail READ detail NOTIFY changed)

public:
    explicit BackfillSkipNote(QObject* parent = nullptr);
    QString person() const { return m_person; }
    void setPerson(const QString& p);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& s);
    int count() const { return m_entries.size(); }
    QString text() const;
    QString detail() const;
    // Test seam (without QML): an explicit facade.
    void setFacade(tanara::processing::IVoiceModelsFacade* facade);

signals:
    void personChanged();
    void changed();

private:
    void reload();
    void resolveServices();
    QPointer<tanara::processing::IVoiceModelsFacade> m_facade;
    bool m_explicitFacade = false;
    QString m_person, m_demoState;
    QVariantList m_entries;
    QList<QMetaObject::Connection> m_connections;
};

} // namespace tanara_qml
