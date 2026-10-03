#include "SettingsDeviceModel.h"

#include "RecorderViewModel.h"
#include "SettingsViewModel.h"

#include "tanara/AppController.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/audio/TrackCatalog.h"

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {
constexpr qint64 kPeakHoldMs = 1500;   // csúcstartás ideje (mint a felvevőben)
}

SettingsDeviceModel::SettingsDeviceModel(SettingsViewModel* vm)
    : QAbstractListModel(vm), m_vm(vm)
{
    m_clock.start();
    m_tick.setInterval(33);   // ~30 Hz: a csúcstartás lecsengése
    connect(&m_tick, &QTimer::timeout, this, &SettingsDeviceModel::tick);
}

int SettingsDeviceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> SettingsDeviceModel::roleNames() const
{
    return {
        {KeyRole, "key"}, {NameRole, "name"}, {RawNameRole, "rawName"},
        {DefaultNameRole, "defaultName"}, {RenamedRole, "renamed"}, {GroupRole, "group"},
        {GroupFirstRole, "groupFirst"}, {SelectedRole, "selected"}, {DefaultRole, "isDefault"},
        {LevelRole, "level"}, {PeakRole, "peak"},
    };
}

QVariant SettingsDeviceModel::data(const QModelIndex& index, int role) const
{
    const int i = index.row();
    if (i < 0 || i >= m_rows.size()) return {};
    const Row& r = m_rows.at(i);
    const QMap<QString, QString>& names = m_vm->draft().deviceNames;
    switch (role) {
    case KeyRole:
    case RawNameRole:     return r.info.name;
    case NameRole:        return devicenames::displayName(r.info.name, names);
    case DefaultNameRole: return devicenames::displayName(r.info.name, {});
    case RenamedRole:     return names.contains(r.info.name);
    case GroupRole:       return r.group;
    case GroupFirstRole:  return i == 0 || m_rows.at(i - 1).group != r.group;
    case SelectedRole:    return m_vm->draftSelectedDevices().contains(r.info.name);
    case DefaultRole:     return r.info.isDefault;
    case LevelRole:       return r.level;
    case PeakRole:        return r.peakSeg;
    }
    return {};
}

int SettingsDeviceModel::rowOf(const QString& name) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).info.name == name) return i;
    return -1;
}

QStringList SettingsDeviceModel::presentNames() const
{
    QStringList out;
    for (const Row& r : m_rows) out << r.info.name;
    return out;
}

void SettingsDeviceModel::rebuild()
{
    AppController* c = m_vm->controller();
    if (!c) {
        loadDemo();
        return;
    }
    const QVector<AudioDeviceInfo> present =
        c->devices() ? c->devices()->captureDevices() : QVector<AudioDeviceInfo>{};

    // Ugyanaz a halmaz → a szintek maradnak, csak az alapértelmezett-jelölés frissül.
    bool same = present.size() == m_rows.size();
    for (int k = 0; same && k < present.size(); ++k) same = rowOf(present.at(k).name) >= 0;

    QVector<Row> rows;
    for (const AudioDeviceInfo& d : present) {
        Row r;
        const int old = rowOf(d.name);
        if (old >= 0) r = m_rows.at(old);
        r.info = d;
        r.group = d.kind == TrackKind::Mic ? 0 : d.kind == TrackKind::Loopback ? 1 : 2;
        rows.push_back(r);
    }
    // A felvevő sorrendje: csoportonként, az alapértelmezett eszköz elöl.
    std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
        if (x.group != y.group) return x.group < y.group;
        return x.info.isDefault && !y.info.isDefault;
    });

    for (int k = 0; same && k < rows.size(); ++k)
        same = rows.at(k).info.name == m_rows.at(k).info.name;   // a sorrend is ugyanaz
    if (same) {
        m_rows = rows;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(m_rows.size() - 1));
        return;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit m_vm->deviceCountChanged();
}

void SettingsDeviceModel::refreshFromDraft()
{
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1),
                         {NameRole, RenamedRole, SelectedRole});
}

void SettingsDeviceModel::toggle(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    QStringList& sel = m_vm->draftSelectedDevices();
    const QString name = m_rows.at(row).info.name;
    if (!sel.removeOne(name)) sel << name;
    emit dataChanged(index(row), index(row), {SelectedRole});
    m_vm->touch();
}

void SettingsDeviceModel::rename(int row, const QString& name)
{
    if (row < 0 || row >= m_rows.size()) return;
    const QString raw = m_rows.at(row).info.name;
    const QString n = name.simplified();
    QMap<QString, QString>& names = m_vm->draft().deviceNames;
    // Üres, vagy épp a gyári rövid név → nincs saját név.
    if (n.isEmpty() || n == devicenames::displayName(raw, {}))
        names.remove(raw);
    else
        names.insert(raw, n);
    emit dataChanged(index(row), index(row), {NameRole, RenamedRole});
    m_vm->touch();
}

void SettingsDeviceModel::onLevel(const QString& deviceName, float rms, float peak)
{
    const int i = rowOf(deviceName);
    if (i < 0) return;
    Row& r = m_rows[i];
    if (r.demoFixed) return;
    const int level = RecorderViewModel::levelSegments(rms);
    const int peakSeg = RecorderViewModel::levelSegments(peak) - 1;
    const qint64 now = m_clock.elapsed();
    bool changed = level != r.level;
    r.level = level;
    if (peakSeg >= r.peakSeg && peakSeg >= 0) {
        changed = changed || peakSeg != r.peakSeg;
        r.peakSeg = peakSeg;
        r.peakAt = now;
    }
    if (changed) emit dataChanged(index(i), index(i), {LevelRole, PeakRole});
}

void SettingsDeviceModel::setTicking(bool on)
{
    if (on) {
        m_tick.start();
        return;
    }
    m_tick.stop();
    // A lap eltűnt: a mérők ne fagyjanak be egy régi értéken.
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.demoFixed || (r.level == 0 && r.peakSeg < 0)) continue;
        r.level = 0;
        r.peakSeg = -1;
        emit dataChanged(index(i), index(i), {LevelRole, PeakRole});
    }
}

void SettingsDeviceModel::tick()
{
    const qint64 now = m_clock.elapsed();
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.demoFixed || r.peakSeg < 0) continue;
        if (now - r.peakAt > kPeakHoldMs) {
            r.peakSeg = -1;
            emit dataChanged(index(i), index(i), {PeakRole});
        }
    }
}

// ---- demó: ugyanazok a kitalált eszközök, mint a felvevő demójában ---------------------

void SettingsDeviceModel::loadDemo()
{
    auto mk = [](const char* raw, int group, bool def, int level, int peak) {
        Row r;
        r.info.name = QString::fromUtf8(raw);
        r.info.kind = group == 0 ? TrackKind::Mic : group == 1 ? TrackKind::Loopback : TrackKind::Other;
        r.info.isDefault = def;
        r.group = group;
        r.level = level;
        r.peakSeg = peak;
        r.demoFixed = true;
        return r;
    };
    beginResetModel();
    m_rows = {
        mk("alsa_input.usb-Trust_USB_Microphone-00.mono-fallback", 0, true, 9, 10),
        mk("alsa_input.pci-0000_00_1f.3.analog-stereo", 0, false, 0, -1),
        mk("Monitor of Sennheiser headset - Kommunikáció", 1, false, 6, 7),
        mk("Monitor of Kanto YU4 - Optikai digitális sztereó", 1, true, 0, -1),
        mk("alsa_input.pci-0000_00_1f.3.analog-stereo.line-in", 2, false, 0, -1),
    };
    endResetModel();
    emit m_vm->deviceCountChanged();
}

} // namespace tanara_qml
