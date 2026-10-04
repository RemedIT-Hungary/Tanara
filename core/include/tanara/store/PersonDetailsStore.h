#pragma once
//
// PersonDetailsStore — a személyek KIEGÉSZÍTŐ adatai (becenevek, szabad szöveges megjegyzés)
// a people.json MELLETT, külön fájlban: <metaadat-mappa>/people-details.json.
//
// Miért külön fájl: a people.json egy sima névlista ({"people": ["A", "B"]}), a
// voiceprints.json névhez kötött lenyomat-lista, és mindkettőt a régebbi buildek is EGÉSZBEN
// újraírják a saját (szűkebb) modelljükből — amit nem ismernek, azt mentéskor eldobnák. Ezért
// ezek alakja változatlan marad, az új adat pedig ide kerül. Régi build ezt a fájlt nem
// olvassa és nem írja: nem tud benne kárt tenni. (Ha egy régi build nevez át valakit, a
// bejegyzés a régi néven marad itt — nem vész el, csak nem látszik, amíg a név vissza nem tér.)
//
// A személy azonosítója itt is a NÉV (kisbetű-függetlenül), ahogy a másik két fájlban.
// Több folyamat is írhatja: minden módosítás zár alatt visszaolvassa a lemez friss állapotát,
// arra alkalmazza a változást, és atomikusan ír (store/SharedFile.h) — ugyanaz a minta, mint
// a PeopleStore / VoiceprintStore esetén. Az ismeretlen (jövőbeli) mezőket megőrzi.
//
#include "tanara/store/SharedFile.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct PersonDetails {
    QString     name;
    QStringList aliases;
    QString     note;
    bool isEmpty() const { return aliases.isEmpty() && note.isEmpty(); }
};

class PersonDetailsStore {
public:
    explicit PersonDetailsStore(const QString& filePath = QString());  // üres → people-details.json

    // Ha a fájl a lemezen megváltozott (másik folyamat írta), újraolvassa. Olvasás előtt hívd.
    void refresh();

    PersonDetails details(const QString& name) const;   // ismeretlen név → üres (a név kitöltve)
    QStringList aliases(const QString& name) const { return details(name).aliases; }
    QVector<PersonDetails> all() const;

    // Becenév felvétele. false: üres, megegyezik a névvel, vagy már szerepel (kisbetű-függetlenül).
    bool addAlias(const QString& name, const QString& alias);
    // Becenév törlése. Vissza: a törölt becenév helye a listában (-1 = nem volt ilyen).
    int removeAlias(const QString& name, const QString& alias);
    // Becenév visszatétele az eredeti helyére (visszavonás).
    bool insertAlias(const QString& name, const QString& alias, int index);
    void setNote(const QString& name, const QString& note);

    // Átnevezés: a bejegyzés az új névre kerül. Ha az új néven már van bejegyzés, a kettő
    // egyesül (becenevek uniója; megjegyzés: a célé, ha üres, a forrásé; ha mindkettő van,
    // egymás alá). oldNameAsAlias: a régi név becenév lesz (ha csak kis-/nagybetűben tér el
    // az újtól, akkor nem).
    void rename(const QString& oldName, const QString& newName, bool oldNameAsAlias);
    void remove(const QString& name);
    // A teljes bejegyzés beállítása (visszavonás). Üres bejegyzés → törlés.
    void set(const PersonDetails& details);

    QString filePath() const { return m_filePath; }

private:
    struct Entry {
        PersonDetails d;
        QJsonObject   raw;   // az ismeretlen mezők megőrzéséhez
    };
    int indexOf(const QString& name) const;
    Entry& ensure(const QString& name);
    void load();
    void reloadIfChanged();
    void persist();

    QString        m_filePath;
    QVector<Entry> m_entries;
    QJsonObject    m_rootExtra;        // a gyökér ismeretlen mezői
    FileStamp      m_stamp;
    bool           m_corrupt = false;  // a lemezen lévő fájl értelmezhetetlen (mentéskor félretesszük)
};

} // namespace tanara
