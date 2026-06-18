#pragma once
//
// Tanara — titok-tár absztrakció (API-kulcsok, tokenek).
//
// MVP: fájl-alapú tár a ~/.tanara/secrets.json-ban (chmod 600).
// Az API szándékosan minimális, hogy később egy QtKeychain backend
// drop-in cserélhesse a fájl-alapút.
//
// TODO: QtKeychain backend — ehhez CMake-módosítás (QtKeychain link) kell,
//       amit ebben a modulban NEM végzünk el.
//
#include <QString>
#include <QHash>

namespace tanara {

class KeyStore {
public:
    // filePath üres → alapértelmezett: <metadataDir vagy ~/.tanara>/secrets.json
    explicit KeyStore(const QString& filePath = QString());

    // Visszaadja a tárolt értéket, vagy üres stringet ha nincs.
    QString get(const QString& key) const;

    // Beállít/felülír egy kulcsot és azonnal perzisztál (chmod 600).
    void set(const QString& key, const QString& value);

    // Töröl egy kulcsot (ha létezik) és perzisztál.
    void remove(const QString& key);

    bool contains(const QString& key) const;

    QString filePath() const { return m_filePath; }

private:
    void load();
    void persist() const;

    QString m_filePath;
    QHash<QString, QString> m_cache;
};

} // namespace tanara
