//
// Beszélő-modellek: a VoiceModelRegistry (leírók, fájl-feloldás, id-normalizálás) és a
// settings.json "voiceModels" kulcsa (SettingsManager). Minden ideiglenes mappában fut.
//
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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
        const QVector<VoiceModelSpec> all = VoiceModelRegistry::builtin();
        QCOMPARE(all.size(), 3);
        // Id szerint ábécérendben.
        QCOMPARE(all[0].id, QStringLiteral("campplus"));
        QCOMPARE(all[1].id, QStringLiteral("eres2netv2"));
        QCOMPARE(all[2].id, QStringLiteral("wespeaker-resnet34-lm"));
        const auto s = VoiceModelRegistry::spec(QStringLiteral("campplus"));
        QVERIFY(s.has_value());
        QCOMPARE(s->id, VoiceModelRegistry::defaultModelId());
        QCOMPARE(s->fileName, QStringLiteral("campplus_sv_zh_en_16k.onnx"));
        QCOMPARE(s->dim, 192);
        QVERIFY(!s->license.isEmpty());
        QVERIFY(s->sourceUrl.startsWith(QStringLiteral("https://")));
        // Az fbank-paraméterek a VoiceEmbedder alapértelmezései.
        const EmbedderConfig def;
        QCOMPARE(s->features.numMelBins, def.numMelBins);
        QCOMPARE(s->features.sampleRate, def.sampleRate);
        QCOMPARE(s->features.subtractMean, def.subtractMean);
        QVERIFY(!VoiceModelRegistry::spec(QStringLiteral("nincs-ilyen")).has_value());
    }

    void resolvePath_andAvailable_singleRule()
    {
        QTemporaryDir meta, app;
        const VoiceModelSpec s = *VoiceModelRegistry::spec(QStringLiteral("campplus"));
        const QString userModel = QDir(meta.path()).filePath(QStringLiteral("models/") + s.fileName);
        const QString appModel = QDir(app.path()).filePath(QStringLiteral("models/") + s.fileName);

        QCOMPARE(VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), userModel);
        QVERIFY(VoiceModelRegistry::available(meta.path(), app.path()).isEmpty());

        touch(appModel);
        QCOMPARE(VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), appModel);
        QCOMPARE(VoiceModelRegistry::available(meta.path(), app.path()).size(), 1);
        QVERIFY(VoiceModelRegistry::available(meta.path(), QString()).isEmpty());

        touch(userModel);
        QCOMPARE(VoiceModelRegistry::resolvePath(s, meta.path(), app.path()), userModel);
        // A régi paths-függvény ugyanezt a szabályt használja.
        QCOMPARE(paths::resolveVoiceModelPath(meta.path(), app.path()),
                 VoiceModelRegistry::resolvePath(s, meta.path(), app.path()));
        QCOMPARE(paths::voiceModelFileName(), s.fileName);
    }

    void normalizeIds_sortedUnique()
    {
        QCOMPARE(VoiceModelRegistry::normalizeIds({" wespeaker ", "campplus", "", "campplus", "eres2net"}),
                 QStringList({"campplus", "eres2net", "wespeaker"}));
        QVERIFY(VoiceModelRegistry::normalizeIds({}).isEmpty());
    }

    void settings_defaultPersistAndSignal()
    {
        QTemporaryDir meta;
        SettingsManager sm(meta.path());
        QCOMPARE(sm.enabledVoiceModels(), QStringList{"campplus"});
        // Első indításkor kiírt fájlban is ott az alapérték.
        QCOMPARE(readJson(sm.settingsFilePath()).value("voiceModels").toArray(), QJsonArray({"campplus"}));

        QSignalSpy changed(&sm, &SettingsManager::settingsChanged);
        sm.setEnabledVoiceModels({"zeta", "campplus", "zeta"});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(sm.enabledVoiceModels(), QStringList({"campplus", "zeta"}));
        QCOMPARE(readJson(sm.settingsFilePath()).value("voiceModels").toArray(), QJsonArray({"campplus", "zeta"}));

        // Ugyanaz (más sorrendben) → nincs változás, nincs jel.
        sm.setEnabledVoiceModels({"zeta", "campplus"});
        QCOMPARE(changed.count(), 1);

        // Visszaolvasás; az üres lista is érvényes (minden modell ki).
        QCOMPARE(SettingsManager(meta.path()).enabledVoiceModels(), QStringList({"campplus", "zeta"}));
        sm.setEnabledVoiceModels({});
        QCOMPARE(changed.count(), 2);
        QVERIFY(SettingsManager(meta.path()).enabledVoiceModels().isEmpty());
    }

    void settings_missingKeyDefaults_unsortedFileNormalized()
    {
        QTemporaryDir meta;
        const QString file = QDir(meta.path()).filePath(QStringLiteral("settings.json"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"userSpeakerName":"Teszt"})");
        }
        QCOMPARE(SettingsManager(meta.path()).enabledVoiceModels(), QStringList{"campplus"});
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(R"({"voiceModels":["b","a","b"]})");
        }
        QCOMPARE(SettingsManager(meta.path()).enabledVoiceModels(), QStringList({"a", "b"}));
    }
};

QTEST_GUILESS_MAIN(VoiceModelsTest)
#include "test_voice_models.moc"
