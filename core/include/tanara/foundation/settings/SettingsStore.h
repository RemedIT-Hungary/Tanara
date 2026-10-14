#pragma once
//
// SettingsStore: settings.json as a set of named JSON sections (format version 2):
//
//   { "version": 2, "paths": {...}, "recording": {...}, "watcher": {...}, ... }
//
// It knows nothing about the meaning of the sections (the typed structs live in their modules;
// app::SettingsManager assembles them). Responsibilities: reading, atomic writing (QSaveFile),
// keeping an unreadable / old-format file aside instead of silently overwriting it, and a
// per-section change signal.
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tanara {

class SettingsStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kVersion = 2;

    enum class LoadResult {
        Missing,     // no file yet (first run): empty sections
        Ok,          // a v2 file was read
        Unreadable,  // exists but cannot be opened / parsed: empty sections, the file is untouched
        OldFormat,   // a file without "version": 2 (the flat v1 layout): empty sections, untouched
    };

    explicit SettingsStore(const QString& filePath, QObject* parent = nullptr);

    QString filePath() const { return m_path; }

    LoadResult load();
    LoadResult lastLoad() const { return m_last; }

    QJsonObject section(const QString& name) const { return m_sections.value(name).toObject(); }
    QStringList sectionNames() const { return m_sections.keys(); }
    // Sets a section in memory; emits sectionChanged(name) if the content differs.
    void setSection(const QString& name, const QJsonObject& value);

    // Writes the file atomically (creates the folder). If the file on disk was unreadable or of the
    // old format at load time, it is first moved aside as "settings.json.corrupt-<ts>" /
    // "settings.json.v1-<ts>". false: the write (or setting aside) failed; the file is untouched.
    bool save();

    // The whole document (version + sections), as written by save().
    QJsonObject document() const;

signals:
    void sectionChanged(const QString& name);

private:
    QString m_path;
    QJsonObject m_sections;
    LoadResult m_last = LoadResult::Missing;
    bool m_setAsideOnSave = false;
};

} // namespace tanara
