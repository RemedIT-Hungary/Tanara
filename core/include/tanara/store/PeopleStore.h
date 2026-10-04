#pragma once
//
// PeopleStore — globális személy-névlista (~/.tanara/people.json), meetingek közt
// újrahasználható a beszélő-átnevezéshez (autocomplete-hez). A meeting-specifikus
// nyers→név leképezés a Meeting.speakerMap-ben él, nem itt.
//
// Több folyamat is írhatja: minden módosítás zár alatt visszaolvassa a lemez friss állapotát
// (ha változott), arra alkalmazza a változást, és atomikusan ír (lásd store/SharedFile.h).
//
#include "tanara/store/SharedFile.h"

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

    // Ha a fájl a lemezen megváltozott (másik folyamat írta), újraolvassa.
    void refresh() { reloadIfChanged(); }

    QString filePath() const { return m_filePath; }

private:
    void load();
    void reloadIfChanged();   // ha a fájl a lemezen megváltozott (másik folyamat írta)
    void persist();

    QString     m_filePath;
    QStringList m_names;
    FileStamp   m_stamp;            // a fájl állapota az utolsó betöltéskor / mentéskor
    bool        m_corrupt = false;  // a lemezen lévő fájl értelmezhetetlen (mentéskor félretesszük)
};

} // namespace tanara
