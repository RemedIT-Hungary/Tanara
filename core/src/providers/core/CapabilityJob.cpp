#include "tanara/providers/core/CapabilityJob.h"

#include <QMetaObject>

namespace tanara::providers {

CapabilityJob::CapabilityJob(QObject* parent) : QObject(parent) {}
CapabilityJob::~CapabilityJob() = default;

void CapabilityJob::cancel()
{
    if (m_cancelled || m_done) return;
    m_cancelled = true;
    onCancel();
}

void CapabilityJob::succeed()
{
    if (m_cancelled || m_done) return;
    m_done = true;
    emit finished();
}

void CapabilityJob::fail(const Error& error)
{
    if (m_cancelled || m_done) return;
    m_done = true;
    m_error = error;
    if (!m_error.isError()) m_error.kind = Error::Kind::Service;
    emit failed(m_error);
}

void CapabilityJob::succeedLater()
{
    QPointer<CapabilityJob> self(this);
    QMetaObject::invokeMethod(this, [self]() { if (self) self->succeed(); }, Qt::QueuedConnection);
}

void CapabilityJob::failLater(const Error& error)
{
    QPointer<CapabilityJob> self(this);
    QMetaObject::invokeMethod(this, [self, error]() { if (self) self->fail(error); }, Qt::QueuedConnection);
}

Error inputError(const QString& code, const QString& message)
{
    Error e;
    e.kind = Error::Kind::Input;
    e.code = code;
    e.message = message;
    return e;
}

} // namespace tanara::providers
