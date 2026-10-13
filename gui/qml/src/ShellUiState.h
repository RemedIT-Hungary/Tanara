#pragma once
//
// ShellUiState: the main window's remembered UI state (window size, selected meeting, player
// volume / speed). It lives in the metadata folder (<TANARA_HOME or ~/.tanara>/ui-state.json),
// so a TANARA_HOME sandbox does not touch the user's real state. Without a controller (demo,
// screenshot, test) it only keeps it in memory and writes nothing to disk.
//
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class ShellUiState : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ShellUiState(QObject* parent = nullptr);
    ~ShellUiState() override;

    // In a test: an explicit file (empty -> no disk).
    void setFilePath(const QString& path);
    QString filePath() const { return m_path; }

    Q_INVOKABLE QVariant value(const QString& key, const QVariant& fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    // Writes the pending changes immediately (on closing the window).
    Q_INVOKABLE void flush();

private:
    void load();

    QString m_path;
    QJsonObject m_data;
    bool m_dirty = false;
    QTimer m_saveTimer;
};

} // namespace tanara_qml
