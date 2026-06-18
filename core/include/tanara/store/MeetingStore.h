#pragma once
//
// Tanara — meeting-tár.
//
// A LEMEZ az igazság forrása: minden meeting egy mappa az audioDir alatt
//   yyyy-MM-dd_HHmm_<titleslug>_<shortid>
// benne meeting.json (JsonSerialization). A <metadataDir>/index.db (Qt6::Sql,
// SQLite) csak gyors listázás céljából cache-eli a metaadatot, bármikor
// újraépíthető a lemezről.
//
#include "tanara/Types.h"
#include <QObject>
#include <QVector>

namespace tanara {

class SettingsManager;

class MeetingStore : public QObject {
    Q_OBJECT
public:
    // Explicit könyvtárakkal.
    explicit MeetingStore(const QString& audioDir,
                          const QString& metadataDir,
                          QObject* parent = nullptr);

    // SettingsManagerből veszi az audio/metadata dir-t.
    explicit MeetingStore(SettingsManager* settings, QObject* parent = nullptr);

    ~MeetingStore() override;

    // Létrehoz egy új meetinget: mappa + üres meeting.json, index-be írva.
    Meeting createMeeting(const QString& title);

    // A meeting.json kiírása a meeting mappájába + index frissítés.
    void saveMeeting(const Meeting& m);

    // Összes meeting a (lemez-cache) indexből, startedAt szerint csökkenőben.
    QVector<Meeting> loadAll();

    // Egy meeting betöltése azonosító alapján (lemezről).
    Meeting load(const QString& id);

    // Teljes index újraépítés az audioDir mappáit végigpásztázva.
    void rebuildIndexFromDisk();

    QString audioDir() const { return m_audioDir; }
    QString metadataDir() const { return m_metadataDir; }

signals:
    void meetingAdded(QString id);
    void meetingUpdated(QString id);

private:
    void openDb();
    void ensureSchema();
    void upsertIndex(const Meeting& m);
    QString dbConnectionName() const;
    QString meetingJsonPath(const QString& folder) const;

    QString m_audioDir;
    QString m_metadataDir;
    QString m_connName;   // egyedi QSqlDatabase connection-név
};

} // namespace tanara
