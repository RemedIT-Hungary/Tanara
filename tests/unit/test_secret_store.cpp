// SecretStore (foundation/secrets): the file backend, the settings section, and the guarantee that
// a sandbox (TANARA_HOME) or the Qt test mode NEVER reaches the real OS keychain.
// The keychain backend itself is not exercised here (it would touch the user's real keychain):
// it is a manual test (see docs/BUILD.md).
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/foundation/secrets/SecretStore.h"

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
};

QTEST_GUILESS_MAIN(SecretStoreTest)
#include "test_secret_store.moc"
