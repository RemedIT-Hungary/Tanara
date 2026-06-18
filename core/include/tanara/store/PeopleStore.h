#pragma once
//
// PeopleStore — globális személy-névlista (~/.tanara/people.json), meetingek közt
// újrahasználható a beszélő-átnevezéshez (autocomplete-hez). A meeting-specifikus
// nyers→név leképezés a Meeting.speakerMap-ben él, nem itt.
//
#include <QString>
#include <QStringList>

namespace tanara {

class PeopleStore {
public:
    explicit PeopleStore(const QString& filePath = QString());  // üres → ~/.tanara/people.json

    QStringList names() const;
    void add(const QString& name);              // hozzáadja, ha még nincs (case-insensitive)
    void rename(const QString& oldName, const QString& newName);  // a névlistában átnevez
    void remove(const QString& name);           // törli a névlistából

    QString filePath() const { return m_filePath; }

private:
    void load();
    void persist() const;

    QString     m_filePath;
    QStringList m_names;
};

} // namespace tanara
