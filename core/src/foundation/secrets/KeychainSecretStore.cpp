#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#ifdef TANARA_HAVE_KEYCHAIN
#  include <QEventLoop>
#  include <QPointer>
#  include <QTimer>
#  include <qt6keychain/keychain.h>
#endif

namespace tanara {

namespace secrets {

bool keychainCompiled()
{
#ifdef TANARA_HAVE_KEYCHAIN
    return true;
#else
    return false;
#endif
}

bool keychainAllowed()
{
    // The sandbox and the tests never reach the user's real keychain.
    if (!paths::homeOverride().isEmpty()) return false;
    if (QStandardPaths::isTestModeEnabled()) return false;
    return keychainCompiled();
}

} // namespace secrets

#ifdef TANARA_HAVE_KEYCHAIN

namespace {

constexpr int kJobTimeoutMs = 5000;

enum class JobOutcome { Ok, NotFound, Failed };

struct JobResult {
    JobOutcome outcome = JobOutcome::Failed;
    QString text;
    QString error;
};

// Runs one QtKeychain job synchronously (local event loop, at most kJobTimeoutMs). On a timeout the
// job keeps running and deletes itself when it finishes (autoDelete); we stop waiting.
JobResult runJob(QKeychain::Job* job)
{
    auto result = std::make_shared<JobResult>();
    auto done = std::make_shared<bool>(false);
    QEventLoop loop;
    QPointer<QEventLoop> guard(&loop);
    job->setAutoDelete(true);
    QObject::connect(job, &QKeychain::Job::finished, job, [result, done, guard](QKeychain::Job* j) {
        if (j->error() == QKeychain::NoError) {
            result->outcome = JobOutcome::Ok;
            if (auto* r = qobject_cast<QKeychain::ReadPasswordJob*>(j)) result->text = r->textData();
        } else if (j->error() == QKeychain::EntryNotFound) {
            result->outcome = JobOutcome::NotFound;
        } else {
            result->outcome = JobOutcome::Failed;
            result->error = j->errorString();
        }
        *done = true;
        if (guard) guard->quit();
    });
    QTimer::singleShot(kJobTimeoutMs, &loop, &QEventLoop::quit);
    job->start();
    if (!*done) loop.exec();
    if (!*done) {
        JobResult timeout;
        timeout.error = QStringLiteral("timeout");
        return timeout;
    }
    return *result;
}

JobResult readKey(const QString& key)
{
    auto* job = new QKeychain::ReadPasswordJob(secrets::KeychainService);
    job->setKey(key);
    return runJob(job);
}

JobResult writeKey(const QString& key, const QString& value)
{
    auto* job = new QKeychain::WritePasswordJob(secrets::KeychainService);
    job->setKey(key);
    job->setTextData(value);
    return runJob(job);
}

JobResult deleteKey(const QString& key)
{
    auto* job = new QKeychain::DeletePasswordJob(secrets::KeychainService);
    job->setKey(key);
    return runJob(job);
}

class KeychainSecretStore final : public SecretStore {
public:
    // Preloads the known keys into the cache.
    explicit KeychainSecretStore(const QStringList& knownKeys)
    {
        for (const QString& k : knownKeys) {
            const JobResult r = readKey(k);
            if (r.outcome == JobOutcome::Ok && !r.text.isEmpty()) m_cache.insert(k, r.text);
            else if (r.outcome == JobOutcome::Failed)
                qCWarning(lcApp).noquote() << "Keychain: read error" << k << r.error;
        }
    }
    QString get(const QString& key) const override
    {
        if (m_cache.contains(key)) return m_cache.value(key);
        // An unknown key (not in the preload list): read on demand, then cached.
        const JobResult r = readKey(key);
        if (r.outcome == JobOutcome::Ok) m_cache.insert(key, r.text);
        return r.text;
    }
    bool set(const QString& key, const QString& value) override
    {
        const JobResult r = writeKey(key, value);
        if (r.outcome != JobOutcome::Ok) {
            qCWarning(lcApp).noquote() << "Keychain: write error" << key << r.error;
            return false;
        }
        m_cache.insert(key, value);
        return true;
    }
    bool remove(const QString& key) override
    {
        const JobResult r = deleteKey(key);
        m_cache.remove(key);
        return r.outcome != JobOutcome::Failed;
    }
    bool contains(const QString& key) const override { return !get(key).isEmpty(); }
    QString backendName() const override { return QStringLiteral("keychain"); }

private:
    mutable QHash<QString, QString> m_cache;
};

// The keychain works: reading a key that does not exist answers "not found" (or a value).
bool probeKeychain(QString* error)
{
    if (!QKeychain::isAvailable()) {
        if (error) *error = QStringLiteral("no keychain backend");
        return false;
    }
    const JobResult r = readKey(QStringLiteral("tanara.probe"));
    if (r.outcome == JobOutcome::Failed && error) *error = r.error;
    return r.outcome != JobOutcome::Failed;
}

} // namespace

#endif // TANARA_HAVE_KEYCHAIN

namespace secrets {

OpenResult open(const SecretSettings& settings, const QString& secretsFile, const QStringList& knownKeys)
{
    OpenResult out;
    const bool wantKeychain = settings.backend != QLatin1String("file");
    if (!wantKeychain) {
        out.store = std::make_unique<FileSecretStore>(secretsFile);
        return out;
    }
    if (!paths::homeOverride().isEmpty() || QStandardPaths::isTestModeEnabled()) {
        // Sandbox / test: the file, silently (this is by design, not a problem to warn about).
        out.store = std::make_unique<FileSecretStore>(secretsFile);
        return out;
    }
#ifdef TANARA_HAVE_KEYCHAIN
    QString error;
    if (QCoreApplication::instance() && probeKeychain(&error)) {
        // Known keys + whatever the old file holds (so the import below sees them in the cache).
        QStringList keys = knownKeys;
        std::unique_ptr<FileSecretStore> legacy;
        if (QFile::exists(secretsFile)) {
            legacy = std::make_unique<FileSecretStore>(secretsFile);
            for (const QString& k : legacy->all().keys())
                if (!keys.contains(k)) keys << k;
        }
        auto store = std::make_unique<KeychainSecretStore>(keys);
        if (legacy) {
            // One-time import: secrets.json -> keychain; the file stays as secrets.json.imported.
            bool allOk = true;
            const auto all = legacy->all();
            for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
                if (it.value().isEmpty()) continue;
                if (store->set(it.key(), it.value())) ++out.imported;
                else allOk = false;
            }
            if (allOk) {
                const QString target = secretsFile + QStringLiteral(".imported");
                QFile::remove(target);
                if (!QFile::rename(secretsFile, target))
                    qCWarning(lcApp).noquote() << "secrets.json cannot be renamed:" << secretsFile;
                else
                    qCInfo(lcApp).noquote() << "Keys moved into the keychain:" << out.imported
                                            << "— old file:" << target;
            } else {
                out.warning = QStringLiteral("import");
                qCWarning(lcApp).noquote() << "Not every key reached the keychain; secrets.json stays.";
            }
        }
        out.store = std::move(store);
        return out;
    }
    out.warning = error.isEmpty() ? QStringLiteral("unavailable") : error;
    qCWarning(lcApp).noquote() << "Keychain not available (" << out.warning << ") — keys in file:" << secretsFile;
#else
    out.warning = QStringLiteral("not compiled");
#endif
    out.store = std::make_unique<FileSecretStore>(secretsFile);
    return out;
}

} // namespace secrets

} // namespace tanara
