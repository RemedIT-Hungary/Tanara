// Speaker models: VoiceModelRegistry (descriptors, file resolution, id normalization) and
// settings.json's "voiceModels" key (SettingsManager). Everything runs in temporary folders.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

using namespace tanara;

namespace {

void touch(const QString& f)
{
    QVERIFY(QDir().mkpath(QFileInfo(f).absolutePath()));
    QFile file(f);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

class VoiceModelsTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("TANARA_HOME", m_home.path().toUtf8());
    }

    void builtin_campplus()
    {
        const QVector<processing::VoiceModelSpec> all = processing::VoiceModelRegistry::builtin();
        QCOMPARE(all.size(), 3);
        // By id alphabetically.
        QCOMPARE(all[0].id, QStringLiteral("campplus"));
        QCOMPARE(all[1].id, QStringLiteral("eres2netv2"));
        QCOMPARE(all[2].id, QStringLiteral("wespeaker-resnet34-lm"));
        const auto s = processing::VoiceModelRegistry::spec(QStringLiteral("campplus"));
        QVERIFY(s.has_value());
        QCOMPARE(s->id, processing::VoiceModelRegistry::defaultModelId());
        QCOMPARE(s->fileName, QStringLiteral("campplus_sv_zh_en_16k.onnx"));
        QCOMPARE(s->dim, 192);
        QVERIFY(!s->license.isEmpty());
        QVERIFY(s->sourceUrl.startsWith(QStringLiteral("https://")));
        // The fbank parameters are VoiceEmbedder's defaults.
        const processing::EmbedderConfig def;
        QCOMPARE(s->features.numMelBins, def.numMelBins);
        QCOMPARE(s->features.sampleRate, def.sampleRate);
        QCOMPARE(s->features.subtractMean, def.subtractMean);
        QVERIFY(!processing::VoiceModelRegistry::spec(QStringLiteral("nincs-ilyen")).has_value());
    }

    void resolvePath_andAvailable_singleRule()
    {
        QTemporaryDir meta, app;
        const processing::VoiceModelSpec s = *processing::VoiceModelRegistry::spec(QStringLiteral("campplus"));
        const QString userModel = QDir(meta.path()).filePath(s.fileName);   // meta = the models folder
        const QString appModel = QDir(app.path()).filePath(QStringLiteral("models/") + s.fileName);

        QCOMPARE(processing::VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), userModel);
        QVERIFY(processing::VoiceModelRegistry::available(meta.path(), app.path()).isEmpty());

        touch(appModel);
        QCOMPARE(processing::VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), appModel);
        QCOMPARE(processing::VoiceModelRegistry::available(meta.path(), app.path()).size(), 1);
        QVERIFY(processing::VoiceModelRegistry::available(meta.path(), QString()).isEmpty());

        touch(userModel);
        QCOMPARE(processing::VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), userModel);
        // The old paths function uses the same rule.
        QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(meta.path(), app.path()),
                 processing::VoiceModelRegistry::resolvePath(s, meta.path(), app.path()));
        QCOMPARE(processing::VoiceModelRegistry::defaultModelFileName(), s.fileName);
    }

    void normalizeIds_sortedUnique()
    {
        QCOMPARE(processing::VoiceModelRegistry::normalizeIds({" wespeaker ", "campplus", "", "campplus", "eres2net"}),
                 QStringList({"campplus", "eres2net", "wespeaker"}));
        QVERIFY(processing::VoiceModelRegistry::normalizeIds({}).isEmpty());
    }

    void settings_defaultPersistAndSignal()
    {
        QTemporaryDir meta;
        app::SettingsManager sm(meta.path());
        QCOMPARE(sm.enabledVoiceModels(), QStringList{"wespeaker-resnet34-lm"});
        // The default is in the file written at first launch too.
        QCOMPARE(readJson(sm.settingsFilePath()).value("processing").toObject().value("voiceModels").toArray(), QJsonArray({"wespeaker-resnet34-lm"}));

        QSignalSpy changed(&sm, &app::SettingsManager::settingsChanged);
        sm.setEnabledVoiceModels({"zeta", "campplus", "zeta"});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(sm.enabledVoiceModels(), QStringList({"campplus", "zeta"}));
        QCOMPARE(readJson(sm.settingsFilePath()).value("processing").toObject().value("voiceModels").toArray(), QJsonArray({"campplus", "zeta"}));

        // The same (in another order): no change, no signal.
        sm.setEnabledVoiceModels({"zeta", "campplus"});
        QCOMPARE(changed.count(), 1);

        // Read back; the empty list is valid too (every model off).
        QCOMPARE(app::SettingsManager(meta.path()).enabledVoiceModels(), QStringList({"campplus", "zeta"}));
        sm.setEnabledVoiceModels({});
        QCOMPARE(changed.count(), 2);
        QVERIFY(app::SettingsManager(meta.path()).enabledVoiceModels().isEmpty());
    }

    void settings_missingKeyDefaults_unsortedFileNormalized()
    {
        QTemporaryDir meta;
        const QString file = QDir(meta.path()).filePath(QStringLiteral("settings.json"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"version":2,"recording":{"userSpeakerName":"Teszt"}})");
        }
        QCOMPARE(app::SettingsManager(meta.path()).enabledVoiceModels(), QStringList{"wespeaker-resnet34-lm"});
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(R"({"version":2,"processing":{"voiceModels":["b","a","b"]}})");
        }
        QCOMPARE(app::SettingsManager(meta.path()).enabledVoiceModels(), QStringList({"a", "b"}));
    }
};

QTEST_GUILESS_MAIN(VoiceModelsTest)
#include "test_voice_models.moc"
