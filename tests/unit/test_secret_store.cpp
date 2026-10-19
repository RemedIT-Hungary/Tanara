// SecretStore (foundation/secrets): the file backend, the settings section, and the guarantee that
// a sandbox (TANARA_HOME) or the Qt test mode NEVER reaches the real OS keychain.
// The keychain backend itself is not exercised here (it would touch the user's real keychain):
// it is a manual test (see docs/BUILD.md). Its cache rules are, with a fake backend
// (CachedSecretStore).
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/foundation/secrets/detail/CachedSecretStore.h"

using namespace tanara;

class SecretStoreTest : public QObject {
    Q_OBJECT
private slots:
    void init() { qunsetenv("TANARA_HOME"); QStandardPaths::setTestModeEnabled(false); }
    void cleanupTestCase() { QStandardPaths::setTestModeEnabled(false); }

    void fileBackendRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sub/secrets.json"));
        {
            FileSecretStore s(path);
            QCOMPARE(s.backendName(), QStringLiteral("file"));
            QVERIFY(!s.contains(QStringLiteral("soniox.apiKey")));
            QVERIFY(s.set(QStringLiteral("soniox.apiKey"), QStringLiteral("abc")));
            QVERIFY(s.set(QStringLiteral("llm.apiKey"), QStringLiteral("def")));
            QVERIFY(s.remove(QStringLiteral("llm.apiKey")));
            QVERIFY(s.remove(QStringLiteral("nincs-ilyen")));
        }
        FileSecretStore back(path);
        QCOMPARE(back.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc"));
        QVERIFY(!back.contains(QStringLiteral("llm.apiKey")));
#if defined(Q_OS_UNIX)
        // Only the owner may read it (0600).
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFileDevice::Permissions());
#endif
    }

    void settingsSection()
    {
        QCOMPARE(SecretSettings().backend, QStringLiteral("auto"));
        QCOMPARE(SecretSettings::fromJson(QJsonObject{{"backend", "FILE"}}).backend, QStringLiteral("file"));
        QCOMPARE(SecretSettings::fromJson(QJsonObject{{"backend", "keychain"}}).backend, QStringLiteral("keychain"));
        QCOMPARE(SecretSettings::fromJson(QJsonObject{{"backend", "valami"}}).backend, QStringLiteral("auto"));
        QCOMPARE(SecretSettings::fromJson(SecretSettings{QStringLiteral("file")}.toJson()).backend, QStringLiteral("file"));
    }

    void explicitFileChoice()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QStandardPaths::setTestModeEnabled(true);   // belt and braces: the keychain stays unreachable
        const secrets::OpenResult r = secrets::open(SecretSettings{QStringLiteral("file")},
                                                    dir.filePath(QStringLiteral("secrets.json")), {});
        QCOMPARE(r.store->backendName(), QStringLiteral("file"));
        QVERIFY(r.warning.isEmpty());
    }

    void sandboxNeverReachesTheKeychain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("secrets.json"));
        {
            FileSecretStore seed(file);
            QVERIFY(seed.set(QStringLiteral("soniox.apiKey"), QStringLiteral("homokozo")));
        }
        qputenv("TANARA_HOME", dir.path().toUtf8());
        QVERIFY(!secrets::keychainAllowed());
        for (const char* backend : {"auto", "keychain"}) {
            const secrets::OpenResult r = secrets::open(SecretSettings{QString::fromLatin1(backend)}, file,
                                                        {QStringLiteral("soniox.apiKey")});
            QCOMPARE(r.store->backendName(), QStringLiteral("file"));
            QCOMPARE(r.imported, 0);
            QCOMPARE(r.store->get(QStringLiteral("soniox.apiKey")), QStringLiteral("homokozo"));
        }
        // No import happened: the file is where it was.
        QVERIFY(QFile::exists(file));
        QVERIFY(!QFile::exists(file + QStringLiteral(".imported")));
    }

    void testModeNeverReachesTheKeychain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(!secrets::keychainAllowed());
        const secrets::OpenResult r = secrets::open(SecretSettings{QStringLiteral("keychain")},
                                                    dir.filePath(QStringLiteral("secrets.json")), {});
        QCOMPARE(r.store->backendName(), QStringLiteral("file"));
    }

    // Regression (crash on macOS 2026-10-17): a keychain read spins a nested event loop, which ran a
    // queued deleteLater() inside a QML click handler. After the preload, get() / contains() must
    // not reach the backend for a key already seen, also when the key does not exist.
    void cacheKeepsMissingKeysAwayFromTheBackend()
    {
        using namespace secrets::detail;
        QHash<QString, QString> backend{{"soniox", "s-key"}};
        int reads = 0;
        bool failReads = false;
        CachedSecretStore store(
            [&](const QString& k) {
                ++reads;
                if (failReads) return OpResult{OpOutcome::Failed, {}, "locked"};
                if (!backend.contains(k)) return OpResult{OpOutcome::NotFound, {}, {}};
                return OpResult{OpOutcome::Ok, backend.value(k), {}};
            },
            [&](const QString& k, const QString& v) { backend.insert(k, v); return OpResult{OpOutcome::Ok, {}, {}}; },
            [&](const QString& k) { backend.remove(k); return OpResult{OpOutcome::Ok, {}, {}}; },
            {"soniox", "openai"}, QStringLiteral("keychain"));
        QCOMPARE(reads, 2);

        // Preloaded keys, present or missing: no backend call.
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(store.get("soniox"), QStringLiteral("s-key"));
            QCOMPARE(store.get("openai"), QString());
            QVERIFY(!store.contains("openai"));
        }
        QCOMPARE(reads, 2);

        // A key outside the preload list: read once, then cached even as "not found".
        QCOMPARE(store.get("other"), QString());
        QVERIFY(!store.contains("other"));
        QCOMPARE(reads, 3);

        // set / remove keep the cache right without reading.
        QVERIFY(store.set("openai", "o-key"));
        QCOMPARE(store.get("openai"), QStringLiteral("o-key"));
        QVERIFY(store.remove("soniox"));
        QVERIFY(!store.contains("soniox"));
        QCOMPARE(reads, 3);

        // A failed read is not cached: the next get() tries again.
        failReads = true;
        QCOMPARE(store.get("late"), QString());
        QCOMPARE(store.get("late"), QString());
        QCOMPARE(reads, 5);
        failReads = false;
        QCOMPARE(store.get("late"), QString());
        QCOMPARE(store.get("late"), QString());
        QCOMPARE(reads, 6);
    }
};

QTEST_GUILESS_MAIN(SecretStoreTest)
#include "test_secret_store.moc"
