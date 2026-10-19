// IPowerInhibitor on Linux: logind first (system bus), the desktop portal second (session bus,
// the one that works inside a Flatpak). Built without Qt DBus it is a no-op ("none").
#include "tanara/foundation/platform/PowerInhibitor.h"

#include "tanara/foundation/log/Logging.h"

#if defined(TANARA_HAVE_QTDBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QVariantMap>
#endif

namespace tanara::platform {

namespace {

#if defined(TANARA_HAVE_QTDBUS)

constexpr int kCallTimeoutMs = 3000;

const QString kLogindService = QStringLiteral("org.freedesktop.login1");
const QString kLogindPath = QStringLiteral("/org/freedesktop/login1");
const QString kLogindIface = QStringLiteral("org.freedesktop.login1.Manager");
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kPortalIface = QStringLiteral("org.freedesktop.portal.Inhibit");
const QString kRequestIface = QStringLiteral("org.freedesktop.portal.Request");
constexpr uint kPortalSuspend = 4;
constexpr uint kPortalIdle = 8;

class LinuxPowerInhibitor final : public IPowerInhibitor {
public:
    ~LinuxPowerInhibitor() override { release(); }

    bool acquire(const QString& reason) override
    {
        if (held()) return true;
        if (tryLogind(reason)) {
            m_backend = QStringLiteral("logind");
            return true;
        }
        if (tryPortal(reason)) {
            m_backend = QStringLiteral("portal");
            return true;
        }
        m_backend = QStringLiteral("none");
        return false;
    }

    void release() override
    {
        // Dropping the last copy of the descriptor closes it, which ends the logind hold.
        m_logindFd = QDBusUnixFileDescriptor();
        if (!m_portalHandle.isEmpty()) {
            QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, m_portalHandle, kRequestIface,
                                                              QStringLiteral("Close"));
            const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, kCallTimeoutMs);
            if (reply.type() == QDBusMessage::ErrorMessage)
                qCWarning(lcApp).noquote() << "Portal Inhibit: Close failed:" << reply.errorMessage();
            m_portalHandle.clear();
        }
    }

    bool held() const override { return m_logindFd.isValid() || !m_portalHandle.isEmpty(); }

    QString backendName() const override
    {
        if (!m_backend.isEmpty()) return m_backend;
        if (QDBusConnection::systemBus().isConnected()) return QStringLiteral("logind");
        if (QDBusConnection::sessionBus().isConnected()) return QStringLiteral("portal");
        return QStringLiteral("none");
    }

private:
    bool tryLogind(const QString& reason)
    {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected()) return false;
        QDBusMessage msg = QDBusMessage::createMethodCall(kLogindService, kLogindPath, kLogindIface,
                                                          QStringLiteral("Inhibit"));
        msg << QStringLiteral("sleep:idle") << QStringLiteral("Tanara") << reason << QStringLiteral("block");
        const QDBusReply<QDBusUnixFileDescriptor> reply = bus.call(msg, QDBus::Block, kCallTimeoutMs);
        if (!reply.isValid() || !reply.value().isValid()) {
            qCInfo(lcApp).noquote() << "logind Inhibit failed:" << reply.error().message();
            return false;
        }
        m_logindFd = reply.value();
        return true;
    }

    bool tryPortal(const QString& reason)
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) return false;
        QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalIface,
                                                          QStringLiteral("Inhibit"));
        QVariantMap options;
        options.insert(QStringLiteral("reason"), reason);
        msg << QString() << uint(kPortalSuspend | kPortalIdle) << options;
        const QDBusReply<QDBusObjectPath> reply = bus.call(msg, QDBus::Block, kCallTimeoutMs);
        if (!reply.isValid() || reply.value().path().isEmpty()) {
            qCInfo(lcApp).noquote() << "Portal Inhibit failed:" << reply.error().message();
            return false;
        }
        m_portalHandle = reply.value().path();
        return true;
    }

    QDBusUnixFileDescriptor m_logindFd;
    QString m_portalHandle;
    QString m_backend;
};

#else

class LinuxPowerInhibitor final : public IPowerInhibitor {
public:
    bool acquire(const QString&) override { return false; }
    void release() override {}
    bool held() const override { return false; }
    QString backendName() const override { return QStringLiteral("none"); }
};

#endif

} // namespace

std::unique_ptr<IPowerInhibitor> makePowerInhibitor()
{
    return std::make_unique<LinuxPowerInhibitor>();
}

} // namespace tanara::platform
