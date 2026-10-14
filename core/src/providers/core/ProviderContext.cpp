#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/foundation/secrets/SecretStore.h"

#include <QNetworkRequest>
#include <QVector>

namespace tanara::providers {

AuthSource AuthSource::apiKey(const SecretStore* store, const QString& secretKey)
{
    AuthSource a;
    a.m_token = [store, secretKey]() { return store ? store->get(secretKey) : QString(); };
    return a;
}

AuthSource AuthSource::bearer(std::function<QString()> token)
{
    AuthSource a;
    a.m_token = std::move(token);
    return a;
}

AuthSource AuthSource::fixed(const QString& token)
{
    AuthSource a;
    a.m_token = [token]() { return token; };
    return a;
}

struct CancelToken::State {
    bool cancelled = false;
    QVector<std::function<void()>> listeners;
};

CancelToken::CancelToken() : d(std::make_shared<State>()) {}

void CancelToken::cancel() const
{
    if (d->cancelled) return;
    d->cancelled = true;
    const auto listeners = std::move(d->listeners);
    d->listeners.clear();
    for (const auto& fn : listeners)
        if (fn) fn();
}

bool CancelToken::isCancelled() const { return d->cancelled; }

void CancelToken::onCancel(std::function<void()> fn) const
{
    if (!fn) return;
    if (d->cancelled) { fn(); return; }
    d->listeners.append(std::move(fn));
}

QString ProviderContext::bearer(const ProviderConfig& cfg) const
{
    return auth.isSet() ? auth.token() : cfg.apiKey;
}

void ProviderContext::applyHeaders(QNetworkRequest& request) const
{
    for (const auto& h : headers)
        request.setRawHeader(h.first, h.second);
}

} // namespace tanara::providers
