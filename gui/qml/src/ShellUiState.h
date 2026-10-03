#pragma once
//
// ShellUiState — a főablak megjegyzett felület-állapota (ablakméret, kijelölt megbeszélés,
// lejátszó hangereje / sebessége). A metaadat-mappában él (<TANARA_HOME vagy ~/.tanara>/
// ui-state.json), így a TANARA_HOME-os homokozó a felhasználó valódi állapotát nem érinti.
// Controller nélkül (demó, képernyőkép, teszt) csak memóriában tart, lemezre nem ír.
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

    // Tesztben: explicit fájl (üres → nincs lemez).
    void setFilePath(const QString& path);
    QString filePath() const { return m_path; }

    Q_INVOKABLE QVariant value(const QString& key, const QVariant& fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    // A függő módosítások azonnali kiírása (ablak bezárásakor).
    Q_INVOKABLE void flush();

private:
    void load();

    QString m_path;
    QJsonObject m_data;
    bool m_dirty = false;
    QTimer m_saveTimer;
};

} // namespace tanara_qml
