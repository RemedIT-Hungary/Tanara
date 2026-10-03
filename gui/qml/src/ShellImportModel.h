#pragma once
//
// ShellImportModel — a „Hangfájl importálása” párbeszédablak (ShellImportDialog.qml) háttere:
// a kiválasztott fájlok a háttérben beolvasott adataikkal (hossz, csatornák, méret), a
// fájlonkénti „csatornánként külön sávra” kapcsoló, a leendő sávok listája és száma, a
// „saját mikrofon” választás, a cím és a dátum (alapból a fájlból, szerkeszthető), majd a
// futó importálás haladása és megszakítása. A munka a core-é (tanara::AudioImporter az
// AppControlleren át); controller nélkül (demó, képernyőkép) kitalált mintaadatot mutat.
//
#include "tanara/import/AudioImporter.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class ShellImportModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // [{ name, path, state: "probing"|"ok"|"error", meta, error, video, canSplit, split,
    //    channels, splitHint }]
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    Q_PROPERTY(bool probing READ probing NOTIFY filesChanged)
    // A leendő sávok megjelenített nevei (a „saját mikrofon” ezek közül választható).
    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY filesChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY filesChanged)
    // „3 sáv lesz belőle.” — a beolvasás alatt és importálható fájl nélkül ennek megfelelő szöveg.
    Q_PROPERTY(QString trackSummary READ trackSummary NOTIFY filesChanged)
    Q_PROPERTY(int ownTrack READ ownTrack WRITE setOwnTrack NOTIFY filesChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY filesChanged)

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    // „2026-03-05 14:30” (szerkeszthető; a dátum elfogadja a pontos / szóközös írást is).
    Q_PROPERTY(QString dateText READ dateText WRITE setDateText NOTIFY dateChanged)
    Q_PROPERTY(bool dateValid READ dateValid NOTIFY dateChanged)
    Q_PROPERTY(QString dateHint READ dateHint NOTIFY dateChanged)

    Q_PROPERTY(bool running READ running NOTIFY runChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY runChanged)
    Q_PROPERTY(int percent READ percent NOTIFY runChanged)            // -1 = még nem mérhető
    Q_PROPERTY(QString progressText READ progressText NOTIFY runChanged)
    Q_PROPERTY(QString runningTitle READ runningTitle NOTIFY runChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged)

    // Képernyőképhez (controller nélkül): "files" | "split" | "probing" | "progress" | "error".
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

public:
    explicit ShellImportModel(QObject* parent = nullptr);

    // Alapból az App-singleton controllere; tesztben injektálható.
    void setController(tanara::AppController* controller);

    QVariantList files() const;
    int fileCount() const { return int(m_rows.size()); }
    bool probing() const;
    QStringList trackNames() const;
    int trackCount() const;
    QString trackSummary() const;
    int ownTrack() const { return m_ownTrack; }
    void setOwnTrack(int index);
    bool canStart() const;

    QString title() const { return m_title; }
    void setTitle(const QString& title);
    QString dateText() const { return m_dateText; }
    void setDateText(const QString& text);
    bool dateValid() const { return parseDate(m_dateText).isValid(); }
    QString dateHint() const;

    bool running() const { return m_running; }
    bool cancelling() const { return m_cancelling; }
    int percent() const { return m_percent; }
    QString progressText() const;
    QString runningTitle() const { return m_runningTitle; }
    QString error() const { return m_error; }
    QString errorDetail() const { return m_errorDetail; }

    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    // Fájlok hozzáadása (helyi utak vagy file:// URL-ek — a húzd-és-ejtsd ezt adja); a már
    // listán lévők és a mappák kimaradnak. Visszaadja, hány került fel.
    Q_INVOKABLE int addFiles(const QVariantList& pathsOrUrls);
    Q_INVOKABLE void removeFile(int row);
    Q_INVOKABLE void setSplit(int row, bool split);
    // Az űrlap kiürítése (futó importálást nem érint).
    Q_INVOKABLE void reset();
    Q_INVOKABLE void clearError();
    // Importálás indítása a mostani űrlapból. false, ha nem indítható.
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();

    // A dátum-mező értelmezése: „2026-03-05 14:30”, „2026. 03. 05. 14:30”, „2026-03-05” …
    static QDateTime parseDate(const QString& text);

signals:
    void filesChanged();
    void titleChanged();
    void dateChanged();
    void runChanged();
    void errorChanged();
    void demoStateChanged();
    // Az importálás elkészült: az új megbeszélés azonosítója (a kijelölést a ShellActions végzi).
    void imported(const QString& meetingId);
    void failed(const QString& message);

private:
    struct Row {
        QString path;
        tanara::ImportFileInfo info;
        bool probed = false;
        bool split = false;
    };
    void attach();
    void onProbed(const tanara::ImportFileInfo& info);
    void refreshDefaults();
    QVector<tanara::ImportSource> sources(QVector<tanara::ImportFileInfo>* infos) const;
    QVector<tanara::ImportPlannedTrack> plan() const;
    void setError(const QString& message, const QString& detail = QString());
    void loadDemo();

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_attached;
    bool m_controllerInjected = false;

    QVector<Row> m_rows;
    int m_ownTrack = -1;
    QString m_title;
    bool m_titleEdited = false;
    QString m_dateText;
    bool m_dateEdited = false;

    bool m_running = false;
    bool m_cancelling = false;
    int m_percent = -1;
    int m_fileIndex = 0;
    int m_fileTotal = 0;
    QString m_importId;
    QString m_runningTitle;
    QString m_error;
    QString m_errorDetail;
    QString m_demoState;
};

} // namespace tanara_qml
