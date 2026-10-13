#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

namespace tanara {

MeetingDetectorRegistry& MeetingDetectorRegistry::instance()
{
    static MeetingDetectorRegistry s_instance;
    return s_instance;
}

void MeetingDetectorRegistry::registerDetector(const DetectorDescriptor& desc, Factory factory)
{
    m_map.insert(desc.id, qMakePair(desc, std::move(factory)));
}

QVector<DetectorDescriptor> MeetingDetectorRegistry::all() const
{
    QVector<DetectorDescriptor> result;
    result.reserve(m_map.size());
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
        result.append(it.value().first);
    return result;
}

bool MeetingDetectorRegistry::has(const QString& id) const
{
    return m_map.contains(id);
}

DetectorDescriptor MeetingDetectorRegistry::descriptor(const QString& id) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return DetectorDescriptor{};
    return it.value().first;
}

IMeetingDetector* MeetingDetectorRegistry::create(const QString& id) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return nullptr;
    const Factory& factory = it.value().second;
    if (!factory)
        return nullptr;
    return factory();
}

IMeetingDetector* MeetingDetectorRegistry::createBest() const
{
    // The first detector that can be created AND is available on this host. An unavailable candidate
    // (e.g. a missing pw-dump) is dropped.
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
        const Factory& factory = it.value().second;
        if (!factory)
            continue;
        IMeetingDetector* det = factory();
        if (det && det->isAvailable())
            return det;
        delete det;
    }
    return nullptr;
}

} // namespace tanara
