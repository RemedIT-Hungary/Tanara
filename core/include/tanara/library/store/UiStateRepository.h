#pragma once
//
// UiStateRepository: small per-meeting view state of the transcript editor that should survive a
// restart (today: whether the review rail is open). One JSON file in the state folder
// (paths::files::UiTranscript), shape: { "rail": { "<meetingId>": true, ... } }. Only the switched-on
// entries are stored (the default is the hidden rail).
#include <QString>

namespace tanara::library {

class UiStateRepository {
public:
    explicit UiStateRepository(const QString& filePath = QString()) : m_path(filePath) {}

    QString filePath() const { return m_path; }
    void setFilePath(const QString& path) { m_path = path; }

    bool railVisible(const QString& meetingId) const;
    // false: no file path / write error.
    bool setRailVisible(const QString& meetingId, bool visible);

private:
    QString m_path;
};

} // namespace tanara::library
