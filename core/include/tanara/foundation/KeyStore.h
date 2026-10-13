#pragma once
//
// Secret-store abstraction (API keys, tokens).
//
// MVP: a file-based store in ~/.tanara/secrets.json (chmod 600).
// The API is deliberately minimal so a QtKeychain backend can later replace the
// file-based one as a drop-in.
//
// TODO: QtKeychain backend: needs a CMake change (linking QtKeychain), which this
// module does NOT do.
#include <QString>
#include <QHash>

namespace tanara {

class KeyStore {
public:
    // Empty filePath = default: <metadataDir or ~/.tanara>/secrets.json
    explicit KeyStore(const QString& filePath = QString());

    // Returns the stored value, or an empty string if there is none.
    QString get(const QString& key) const;

    // Sets/overwrites a key and persists at once (chmod 600).
    void set(const QString& key, const QString& value);

    // Deletes a key (if it exists) and persists.
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
