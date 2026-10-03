#include "ShellUiState.h"

#include "AppContext.h"

#include "tanara/AppController.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QJsonDocument>
#include <QSaveFile>

namespace tanara_qml {

ShellUiState::ShellUiState(QObject* parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &ShellUiState::flush);

    // Csak valódi controllerrel van lemez-állapot (a demó / képernyőkép nem ír sehova).
    if (tanara::AppController* c = AppContext::instance()->controller()) {
        if (c->settings())
            setFilePath(tanara::paths::metadataFile(QStringLiteral("ui-state.json"),
                                                    c->settings()->settings().metadataDir));
    }
}

ShellUiState::~ShellUiState()
{
    flush();
}

void ShellUiState::setFilePath(const QString& path)
{
    flush();
    m_path = path;
    load();
}

void ShellUiState::load()
{
    m_data = {};
    if (m_path.isEmpty())
        return;
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly))
        m_data = QJsonDocument::fromJson(f.readAll()).object();
}

QVariant ShellUiState::value(const QString& key, const QVariant& fallback) const
{
    const auto it = m_data.constFind(key);
    return it == m_data.constEnd() || it->isNull() ? fallback : it->toVariant();
}

void ShellUiState::setValue(const QString& key, const QVariant& value)
{
    // QML-ből JS-objektum / tömb QJSValue-ként érkezik → előbb sima QVariant-tá alakítjuk.
    const QJsonValue v = QJsonValue::fromVariant(
        value.userType() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant() : value);
    if (m_data.value(key) == v)
        return;
    m_data.insert(key, v);
    m_dirty = true;
    if (!m_path.isEmpty())
        m_saveTimer.start();
}

void ShellUiState::flush()
{
    m_saveTimer.stop();
    if (!m_dirty || m_path.isEmpty())
        return;
    m_dirty = false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

} // namespace tanara_qml
