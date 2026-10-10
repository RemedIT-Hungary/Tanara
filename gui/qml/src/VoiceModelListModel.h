#pragma once
//
// VoiceModelListModel — a beszélő-felismerő hangmodellek listája (Beállítások › Beszélők,
// Első lépések): letöltés, bekapcsolás, a lenyomat-pótlás állapota.
//
//  - sorok: a VoiceModelRegistry beépített leírói (név, egy mondat, licenc, forrás, méret,
//    dimenzió) + állapot: missing | downloading | downloaded | bundled | error | offline;
//  - letöltés: a core VoiceModelDownloader (azonnali, NEM piszkozat); sikeres letöltés után a
//    modell a piszkozatban bekapcsolódik (Mentéssel lép életbe);
//  - bekapcsolás: a Beállítások piszkozatába (setDraftAccess: a SettingsViewModel adja); a
//    piszkozat nélküli (önálló) példány a core beállítását írja azonnal;
//  - lenyomat-pótlás: AppController::voiceprintBackfillRunning / voiceprintBackfillFinished;
//    a „k / N” és a pótolhatatlan minták száma a VoiceprintStore-ból (mely mintákból hiányzik
//    még valamelyik használt modell lenyomata) számolódik.
//
// Controller nélkül (demó, --qml-shot) KITALÁLT adatot ad; demoState: B08 | B08download |
// B08none | B08error | B08offline | B08backfill | B08partial | B08bundled.
//
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace tanara {
class AppController;
class VoiceModelDownloader;
}

namespace tanara_qml {

class VoiceModelListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(int count READ count NOTIFY summaryChanged)
    // Van-e legalább egy használható (letöltött vagy csomagolt) modell.
    Q_PROPERTY(bool anyAvailable READ anyAvailable NOTIFY summaryChanged)
    // Bekapcsolt ÉS meglévő modellek száma (a piszkozat szerint) — 0: a hangelemzés ki van kapcsolva.
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY summaryChanged)
    // Az alapmodell (CAM++) — az onboarding és az üres állapot CTA-ja.
    Q_PROPERTY(QString defaultState READ defaultState NOTIFY summaryChanged)
    Q_PROPERTY(QString defaultSizeText READ defaultSizeText CONSTANT)
    Q_PROPERTY(QString defaultProgressText READ defaultProgressText NOTIFY summaryChanged)
    Q_PROPERTY(double defaultProgress READ defaultProgress NOTIFY summaryChanged)   // -1: ismeretlen
    Q_PROPERTY(QString defaultErrorText READ defaultErrorText NOTIFY summaryChanged)

    // ---- lenyomat-pótlás ----
    // "" (nincs mit mutatni) | "running" | "done" | "partial" (maradt pótolhatatlan minta)
    Q_PROPERTY(QString backfillState READ backfillState NOTIFY backfillChanged)
    Q_PROPERTY(int backfillTotal READ backfillTotal NOTIFY backfillChanged)
    Q_PROPERTY(int backfillDone READ backfillDone NOTIFY backfillChanged)
    Q_PROPERTY(int backfillSkipped READ backfillSkipped NOTIFY backfillChanged)
    Q_PROPERTY(double backfillProgress READ backfillProgress NOTIFY backfillChanged)   // -1: határozatlan
    Q_PROPERTY(QString backfillText READ backfillText NOTIFY backfillChanged)

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
        StateRole,          // missing | downloading | downloaded | bundled | error | offline
        ProgressRole,       // 0..1 (letöltés közben), -1 ha ismeretlen
        ProgressTextRole,   // „12 / 27 MB”
        ErrorTextRole,
        EnabledRole,        // a piszkozat szerint bekapcsolva
        CanEnableRole,      // a fájl megvan (letöltött / csomagolt)
        RecommendedRole,
        NotRecommendedRole,
    };
    Q_ENUM(Role)

    explicit VoiceModelListModel(QObject* parent = nullptr);
    ~VoiceModelListModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* controllerObject() const;
    void setControllerObject(QObject* controller);
    void setController(tanara::AppController* controller);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    int count() const { return m_rows.size(); }
    bool anyAvailable() const;
    int activeCount() const;
    bool offline() const { return m_offline; }
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

    // ---- műveletek ----
    // Letöltés indítása (offline-ban nem indul: az állapot „offline” lesz).
    Q_INVOKABLE void download(const QString& id);
    // Újra: hiba után; sérült (eltérő méretű) fájlt is lecserél.
    Q_INVOKABLE void retry(const QString& id);
    Q_INVOKABLE void cancel(const QString& id);
    // Bekapcsolás a piszkozatban (csak meglévő modellnél).
    Q_INVOKABLE void setEnabled(const QString& id, bool on);
    // A forrás-lap (licenc, szerzők) megnyitása: openUrlRequested.
    Q_INVOKABLE void openSource(const QString& id);
    // Az állapot újraolvasása (fájlok, hálózat, pótlás).
    Q_INVOKABLE void refresh();

    // ---- a SettingsViewModelnek ----
    // A piszkozat elérése: get → bekapcsolt id-k, set → új lista (a vm számolja a változást).
    void setDraftAccess(std::function<QStringList()> get, std::function<void(const QStringList&)> set);
    // A piszkozat kívülről változott (betöltés, eldobás, mentés).
    void notifyDraftChanged();
    // Demó: a kitalált állapot (a SettingsViewModel loadDemo-ja hívja).
    void loadDemo(const QString& state);

    // ---- teszt-varratok ----
    tanara::VoiceModelDownloader* downloader() const { return m_downloader; }
    // A leírók cseréje (pl. file:// letöltési URL-ekkel) és a mappák.
    void setSpecsForTest(const QVector<tanara::VoiceModelSpec>& specs);
    void setDirsForTest(const QString& metaDir, const QString& appDir);

signals:
    void controllerChanged();
    void demoStateChanged();
    void summaryChanged();
    void backfillChanged();
    void openUrlRequested(const QString& url);
    // Egy letöltés végzett (ok: sikerült).
    void downloadFinished(const QString& id, bool ok);

private:
    struct Row {
        tanara::VoiceModelSpec spec;
        QString state = QStringLiteral("missing");
        qint64 received = 0, total = 0;
        QString errorText;
        bool corrupt = false;       // a meglévő fájl mérete eltér a várttól
    };

    void attach();
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
    // A pótlás figyelése.
    void checkBackfill();
    void onBackfillFinished(int added);
    void pollBackfill();
    int countMissingSamples() const;
    void setBackfill(const QString& state, int total, int done, int skipped);

    QPointer<tanara::AppController> m_controller;
    bool m_controllerSet = false;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;
    tanara::VoiceModelDownloader* m_downloader = nullptr;

    QVector<Row> m_rows;
    QVector<tanara::VoiceModelSpec> m_specOverride;
    QString m_metaOverride, m_appOverride;
    bool m_dirsOverridden = false;
    std::function<QStringList()> m_getDraft;
    std::function<void(const QStringList&)> m_setDraft;
    QStringList m_demoEnabled;
    bool m_offline = false;

    QString m_bfState;
    int m_bfTotal = 0, m_bfDone = 0, m_bfSkipped = 0, m_bfAdded = 0;
    bool m_bfSeenRunning = false;
    QTimer m_bfTimer;
};

} // namespace tanara_qml
