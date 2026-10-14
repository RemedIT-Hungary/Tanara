#include "demo/RecorderDemoFacade.h"

#include "demo/TagDemoBackend.h"
#include "demo/TagPool.h"

#include "demo/DemoText.h"

#include <QDate>
#include <QTime>

using namespace tanara;
using tanara::recording::DevicePin;
using tanara::recording::RecorderTag;

namespace tanara_demo {

namespace {

// The fictional device names in the English demo language (English counterparts of the Hungarian
// system descriptions).
QString deviceText(const char* hu)
{
    static const QHash<QString, QString> en{
        {QStringLiteral("Trust USB mikrofon"), QStringLiteral("Trust USB microphone")},
        {QStringLiteral("Sennheiser fejhallgató (analóg)"), QStringLiteral("Sennheiser headphones (analog)")},
        {QStringLiteral("Monitor of Sennheiser headset - Kommunikáció"),
         QStringLiteral("Monitor of Sennheiser headset - Communication")},
        {QStringLiteral("Kanto YU4 hangfal"), QStringLiteral("Kanto YU4 speakers")},
        {QStringLiteral("Monitor of Kanto YU4 - Optikai digitális sztereó"),
         QStringLiteral("Monitor of Kanto YU4 - Digital Stereo (IEC958)")},
        {QStringLiteral("Monitor of Sennheiser fejhallgató (analóg sztereó)"),
         QStringLiteral("Monitor of Sennheiser headphones (analog stereo)")},
        {QStringLiteral("Alaplapi vonalbemenet"), QStringLiteral("Motherboard line-in")},
    };
    const QString s = QString::fromUtf8(hu);
    return demotext::english() ? en.value(s, s) : s;
}

TrackKind kindOf(int group)
{
    return group == 0 ? TrackKind::Mic : group == 1 ? TrackKind::Loopback : TrackKind::Other;
}

// The shared made-up tag pool (demo/TagPool.h) the recorder's tag input would offer.
RecorderTag poolTag(const PoolTag& p) { return {QString::fromLatin1(p.id), p.name()}; }

} // namespace

RecorderDemoFacade::RecorderDemoFacade(const QString& designState, Fixture fixture, QObject* parent)
    : IRecorderFacade(parent), m_fixture(fixture), m_design(designState)
{
    if (m_fixture == Fixture::SettingsList)
        loadSettingsList();
    else if (!m_design.isEmpty())
        loadRecorderState(m_design);
}

void RecorderDemoFacade::setDesignState(const QString& state)
{
    m_design = state;
    if (m_fixture == Fixture::SettingsList)
        loadSettingsList();
    else
        loadRecorderState(state);
    emit devicesChanged();
    emit sessionChanged();
}

const RecorderDemoFacade::Device* RecorderDemoFacade::find(const QString& name) const
{
    for (const Device& d : m_devices)
        if (d.info.name == name) return &d;
    return nullptr;
}

QVector<AudioDeviceInfo> RecorderDemoFacade::captureDevices() const
{
    QVector<AudioDeviceInfo> out;
    for (const Device& d : m_devices) out << d.info;
    return out;
}

QString RecorderDemoFacade::friendlyName(const QString& deviceName) const
{
    const Device* d = find(deviceName);
    return d ? d->friendly : deviceName;
}

std::optional<DevicePin> RecorderDemoFacade::pinnedDisplay(const QString& deviceName) const
{
    const Device* d = find(deviceName);
    if (!d) return std::nullopt;
    return d->pin;
}

QString RecorderDemoFacade::appForOutput(const QString& deviceName) const
{
    const Device* d = find(deviceName);
    return d ? d->app : QString();
}

QStringList RecorderDemoFacade::knownDevices() const
{
    QStringList out;
    for (const Device& d : m_devices) out << d.info.name;
    return out;
}

void RecorderDemoFacade::start(const QString&, const QVector<AudioDeviceInfo>&)
{
    if (!m_design.isEmpty()) setDesignState(QStringLiteral("R03"));
}

void RecorderDemoFacade::stop()
{
    if (!m_design.isEmpty()) setDesignState(QStringLiteral("R09"));
}

void RecorderDemoFacade::beginNewSession()
{
    if (!m_design.isEmpty()) setDesignState(QStringLiteral("R01"));
}

bool RecorderDemoFacade::addDevice(const AudioDeviceInfo& device)
{
    if (!m_recording || m_recorded.contains(device.name)) return false;
    m_recorded << device.name;
    if (!m_lastUsed.contains(device.name)) m_lastUsed << device.name;
    emit trackAdded(device.name);
    return true;
}

bool RecorderDemoFacade::stopDevice(const QString& deviceName)
{
    if (!m_recorded.removeOne(deviceName)) return false;
    m_lastUsed.removeOne(deviceName);
    emit trackClosed(deviceName);
    return true;
}

RecorderTag RecorderDemoFacade::createTag(const QString& name)
{
    // The fictional pool (the input list sees the same); an unknown name becomes a new tag.
    for (const PoolTag& p : tagpool::all()) {
        const RecorderTag t = poolTag(p);
        if (t.name.compare(name, Qt::CaseInsensitive) == 0) return t;
    }
    const RecorderTag created{QStringLiteral("new-") + name, name};
    m_createdTags.insert(created.id, created);
    return created;
}

tanara::library::TagBackend* RecorderDemoFacade::tagBackend() const
{
    if (!m_tagBackend)
        m_tagBackend = new TagDemoBackend(TagDemoBackend::Content::Sample, const_cast<RecorderDemoFacade*>(this));
    return m_tagBackend;
}

std::optional<RecorderTag> RecorderDemoFacade::tag(const QString& id) const
{
    for (const PoolTag& p : tagpool::all())
        if (id == QLatin1String(p.id)) return poolTag(p);
    const auto it = m_createdTags.constFind(id);
    if (it != m_createdTags.constEnd()) return it.value();
    return std::nullopt;
}

// ---- the design states -------------------------------------------------------------------

void RecorderDemoFacade::loadRecorderState(const QString& st)
{
    const bool rec = st == QLatin1String("R03") || st == QLatin1String("R04")
                  || st == QLatin1String("R05") || st == QLatin1String("R06")
                  || st == QLatin1String("R07");
    const bool quiet = st == QLatin1String("R06");
    const bool done = st == QLatin1String("R09");
    const QDateTime when(QDate(2026, 10, 3), QTime(14, 2));

    auto mk = [&](const char* friendly, const char* raw, int group, bool sel, bool def, const char* app,
                  int level, int peak, const char* status, const QString& text) {
        Device d;
        d.info.name = deviceText(raw);
        d.info.kind = kindOf(group);
        d.info.isDefault = def;
        d.friendly = deviceText(friendly);
        d.selected = sel;
        d.app = QString::fromUtf8(app);
        d.pin.level = quiet ? 0 : level;
        d.pin.peakSegment = quiet ? -1 : peak;
        d.pin.status = QString::fromLatin1(status);
        d.pin.statusText = text;
        return d;
    };
    m_devices.clear();
    if (st != QLatin1String("R10")) {
        const QString kantoText = rec ? tr("%n perce nincs jel", nullptr, 3) : tr("nincs jel");
        m_devices
            << mk("Trust USB mikrofon", "alsa_input.usb-Trust_USB_microphone-00.mono-fallback",
                  0, true, true, "", 9, 10, "", QString())
            << mk("Sennheiser fejhallgató (analóg)", "alsa_input.pci-0000_00_1f.3.analog-stereo",
                  0, false, false, "", 0, -1, "", QString())
            << mk("Sennheiser headset", "Monitor of Sennheiser headset - Kommunikáció",
                  1, true, false, "Microsoft Teams", rec ? 5 : 7, rec ? 8 : 9, "", QString())
            << mk("Kanto YU4 hangfal", "Monitor of Kanto YU4 - Optikai digitális sztereó",
                  1, true, true, "", 0, -1, quiet ? "" : (rec ? "silentWarn" : "noSignal"),
                  quiet ? QString() : kantoText)
            << mk("Sennheiser fejhallgató (analóg)", "Monitor of Sennheiser fejhallgató (analóg sztereó)",
                  1, false, false, "Firefox", 6, 7, "signalUnrecorded", tr("jel van · nincs rögzítve"))
            << mk("Alaplapi vonalbemenet", "alsa_input.pci-0000_00_1f.3.analog-stereo.line-in",
                  2, false, false, "", 0, -1, "", QString());
    }
    // The selection and the open tracks follow from the made-up devices.
    m_lastUsed.clear();
    m_recorded.clear();
    for (const Device& d : std::as_const(m_devices)) {
        if (d.selected) m_lastUsed << d.info.name;
        if (rec && d.selected) m_recorded << d.info.name;
    }
    m_recording = rec;

    m_session = {};
    const bool namedTitle = st != QLatin1String("R01") && st != QLatin1String("R10");
    if (namedTitle) {
        m_session.title = demoText("Ügyféltámogatás átadás-átvétel", "Customer support handover");
    } else {
        m_session.automaticApp = st == QLatin1String("R10") ? QString() : QStringLiteral("Teams");
        m_session.automaticWhen = when;
    }
    // Fictional tags: one before starting, two during recording.
    if (st != QLatin1String("R09") && st != QLatin1String("R10")) {
        m_session.tags.append(poolTag(tagpool::all()[0]));
        if (rec) m_session.tags.append(poolTag(tagpool::all()[1]));
    }
    m_session.elapsedMs = quiet ? (41 * 60 + 3) * 1000 : rec ? (12 * 60 + 47) * 1000 : 0;
    if (quiet) m_session.askText = tr("A hívás sávján %n perce csend van. Magamtól nem állítom le.", nullptr, 2);
    if (done) {
        m_session.done = true;
        m_session.doneSummary = QStringLiteral("30:34 · ") + tr("%n sáv", nullptr, 3);
        m_session.doneMeetingId = QStringLiteral("demo");
    }
}

void RecorderDemoFacade::loadSettingsList()
{
    auto mk = [](const QString& raw, int group, bool def, int level, int peak) {
        Device d;
        d.info.name = raw;
        d.info.kind = kindOf(group);
        d.info.isDefault = def;
        d.friendly = raw;
        d.pin.level = level;
        d.pin.peakSegment = peak;
        return d;
    };
    m_devices = {
        mk(QStringLiteral("alsa_input.usb-Trust_USB_Microphone-00.mono-fallback"), 0, true, 9, 10),
        mk(QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo"), 0, false, 0, -1),
        mk(demoText("Monitor of Sennheiser headset - Kommunikáció",
                    "Monitor of Sennheiser headset - Communication"), 1, false, 6, 7),
        mk(demoText("Monitor of Kanto YU4 - Optikai digitális sztereó",
                    "Monitor of Kanto YU4 - Digital Stereo (IEC958)"), 1, true, 0, -1),
        mk(QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo.line-in"), 2, false, 0, -1),
    };
    m_session = {};
}

} // namespace tanara_demo
