// P10 capability ports, the LOCAL adapters (no ONNX, no network): the job semantics, the ONNX voice
// embedder with a fake model loader (PCM and clip input through the fake media tool), the local
// participant identifier (the voiceprint matching rule), the LLM summarizer (quick + complex) and
// the LLM tag suggester over a scripted chat provider, the similarity tag suggester, the local voice
// model catalog (list, existing file, file:// download, unknown id) and the registered descriptors.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/SimilarityTagSuggestionProvider.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/ProcessingProviders.h"
#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/processing/analysis/LocalParticipantIdentifier.h"
#include "tanara/processing/summary/LlmSummarizer.h"
#include "tanara/processing/voiceid/LocalVoiceModelCatalog.h"
#include "tanara/processing/voiceid/OnnxVoiceEmbedder.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "fake_media_tool.h"
#include "tags_fixture.h"

using namespace tanara;

namespace {

// ---- scripted chat provider ----
class FakeLlmJob : public providers::LlmJob {
    Q_OBJECT
public:
    explicit FakeLlmJob(QObject* parent) : providers::LlmJob(parent) {}
    void cancel() override {}
};

class ScriptedChat : public QObject, public providers::IChatProvider {
    Q_OBJECT
public:
    std::function<QString(const providers::LlmRequest&)> script;
    QVector<providers::LlmRequest> requests;
    QString name() const override { return QStringLiteral("scripted"); }
    bool supportsStreaming() const override { return false; }
    providers::LlmJob* chat(const providers::LlmRequest& req) override
    {
        requests.append(req);
        auto* job = new FakeLlmJob(this);
        const QString text = script ? script(req) : QString();
        QTimer::singleShot(0, job, [job, text]() { emit job->finished(text); });
        return job;
    }
};

// ---- fake voice model loader: a fixed direction per model, "too short" below 0.5 s ----
processing::PcmEmbedderLoader fakeLoader()
{
    return [](const processing::VoiceModelSpec& spec, const QString&, QString*) -> processing::PcmEmbedder {
        const int dim = spec.dim > 0 ? spec.dim : 4;
        const int hot = spec.id == QLatin1String("alpha") ? 0 : 1;
        return [dim, hot](const QVector<float>& pcm) {
            if (pcm.size() < 8000) return QVector<float>();
            QVector<float> v(dim, 0.0f);
            v[hot] = 1.0f;
            return v;
        };
    };
}

processing::VoiceModelSpec spec(const QString& id, int dim = 4)
{
    processing::VoiceModelSpec s;
    s.id = id;
    s.dim = dim;
    s.fileName = id + QStringLiteral(".onnx");
    return s;
}

QVector<float> unit(int dim, int hot, float other = 0.0f)
{
    QVector<float> v(dim, other);
    v[hot] = 1.0f;
    float n = 0;
    for (float x : v) n += x * x;
    n = std::sqrt(n);
    for (float& x : v) x /= n;
    return v;
}

Voiceprint print(const QString& model, const QVector<float>& e)
{
    Voiceprint p;
    p.model = model;
    p.embedding = e;
    p.dim = int(e.size());
    return p;
}

MergedTranscript shortTranscript()
{
    MergedTranscript t;
    t.language = QStringLiteral("hu");
    for (int i = 0; i < 6; ++i) {
        TranscriptToken tok;
        tok.text = QStringLiteral(" A migráció állapotát egyeztettük, %1. pont.").arg(i + 1);
        tok.speaker = i % 2 ? QStringLiteral("Béla") : QStringLiteral("Ádám");
        tok.startMs = i * 30000;
        tok.endMs = tok.startMs + 20000;
        t.tokens.append(tok);
    }
    return t;
}

template <class Job>
bool waitDone(Job* job, int ms = 5000)
{
    QSignalSpy ok(job, &providers::CapabilityJob::finished);
    QSignalSpy bad(job, &providers::CapabilityJob::failed);
    if (job->isDone()) return true;
    QDeadlineTimer dl(ms);
    while (ok.isEmpty() && bad.isEmpty() && !dl.hasExpired()) QTest::qWait(10);
    return !ok.isEmpty() || !bad.isEmpty();
}

} // namespace

class CapabilityPortsTest : public QObject {
    Q_OBJECT
private slots:
    void job_deliversLater_andCancelSilences();
    void onnxVoiceEmbedder_pcmAndClips();
    void onnxVoiceEmbedder_noModel();
    void localIdentifier_matchingRule();
    void llmSummarizer_quick();
    void llmSummarizer_complex();
    void llmTagSuggester_overChat();
    void similarityTagSuggester();
    void localVoiceCatalog();
    void registeredDescriptors();
};

void CapabilityPortsTest::job_deliversLater_andCancelSilences()
{
    QObject owner;
    auto* job = new providers::FunctionJob<int>([](int* out, Error*) { *out = 42; return true; }, &owner);
    QSignalSpy ok(job, &providers::CapabilityJob::finished);
    QCOMPARE(ok.count(), 0);              // never synchronously
    QVERIFY(ok.wait(1000));
    QCOMPARE(job->result(), 42);

    auto* bad = new providers::FunctionJob<int>([](int*, Error* e) { e->kind = Error::Kind::Input; return false; }, &owner);
    QSignalSpy failed(bad, &providers::CapabilityJob::failed);
    QVERIFY(failed.wait(1000));
    QCOMPARE(bad->error().kind, Error::Kind::Input);

    auto* cancelled = new providers::FunctionJob<int>([](int* out, Error*) { *out = 1; return true; }, &owner);
    QSignalSpy any(cancelled, &providers::CapabilityJob::finished);
    cancelled->cancel();
    QTest::qWait(50);
    QCOMPARE(any.count(), 0);
    QVERIFY(cancelled->isCancelled());
}

void CapabilityPortsTest::onnxVoiceEmbedder_pcmAndClips()
{
    auto media = std::make_shared<testing::FakeMediaTool>();
    media->pcm.insert(QStringLiteral("track_mic.ogg"), QVector<qint16>(16000 * 4, 1000));   // 4 s
    processing::VoiceEmbedderSet set({ { spec("alpha"), "/m/alpha.onnx" }, { spec("beta"), "/m/beta.onnx" } },
                                     fakeLoader());
    processing::OnnxVoiceEmbedder emb(set, media);
    QCOMPARE(emb.defaultModelIds(), QStringList({ "alpha", "beta" }));

    // PCM: two slices, the second too short (an empty vector, the job still succeeds).
    providers::VoiceEmbeddingRequest req;
    req.pcm = { QVector<float>(16000, 0.1f), QVector<float>(100, 0.1f) };
    providers::VoiceEmbeddingJob* job = emb.embed(req);
    QVERIFY(waitDone(job));
    QVERIFY(!job->error().isError());
    const QVector<providers::VoiceVector> out = job->result();
    QCOMPARE(out.size(), 4);   // 2 inputs x 2 models
    QCOMPARE(out[0].modelId, QStringLiteral("alpha"));
    QCOMPARE(out[0].input, 0);
    QCOMPARE(out[0].vector, unit(4, 0));
    QCOMPARE(out[1].vector, unit(4, 1));
    QCOMPARE(out[1].dim, 4);
    QVERIFY(out[2].vector.isEmpty());

    // Clips: decoded through the media tool, one requested model only.
    providers::VoiceEmbeddingRequest clips;
    clips.clips = { { QStringLiteral("/rec/track_mic.ogg"), 500, 2500 } };
    clips.modelIds = { QStringLiteral("beta") };
    job = emb.embed(clips);
    QVERIFY(waitDone(job));
    QCOMPARE(job->result().size(), 1);
    QCOMPARE(job->result()[0].modelId, QStringLiteral("beta"));
    QCOMPARE(job->result()[0].vector, unit(4, 1));
    QCOMPARE(media->decodes().size(), 1);
    QCOMPARE(media->decodes()[0].range.startMs, qint64(500));
    QCOMPARE(media->decodes()[0].format.sampleRate, 16000);

    // Destroying a running job joins its worker (no crash, no signal).
    job = emb.embed(req);
    delete job;
}

void CapabilityPortsTest::onnxVoiceEmbedder_noModel()
{
    processing::OnnxVoiceEmbedder emb(processing::VoiceEmbedderSet({}, fakeLoader()), nullptr);
    providers::VoiceEmbeddingRequest req;
    req.pcm = { QVector<float>(16000, 0.1f) };
    providers::VoiceEmbeddingJob* job = emb.embed(req);
    QVERIFY(waitDone(job));
    QCOMPARE(job->error().kind, Error::Kind::Unsupported);
    QCOMPARE(job->error().code, QStringLiteral("no_voice_model"));
}

void CapabilityPortsTest::localIdentifier_matchingRule()
{
    providers::IdentifyRequest req;
    providers::IdentifyProbe p;
    p.trackId = QStringLiteral("loopback");
    p.windows = 7;
    p.sampleRef = QStringLiteral("track_loopback.ogg#0-3000");
    p.vectors = { { unit(4, 0, 0.1f), 4, QStringLiteral("alpha"), 0 }, { unit(4, 2), 4, QStringLiteral("beta"), 0 } };
    req.probes = { p };
    // Anna: alpha matches well, beta exactly -> mean high. Béla: only an alpha print, orthogonal.
    const PersonId bela = PersonId::fromString(QStringLiteral("p-bela"));
    const PersonId anna = PersonId::fromString(QStringLiteral("p-anna"));
    req.candidates = { { QStringLiteral("Béla"), { print("alpha", unit(4, 3)) }, bela },
                       { QStringLiteral("Anna"), { print("alpha", unit(4, 1)), print("alpha", unit(4, 0)),
                                                   print("beta", unit(4, 2)) }, anna } };

    const QVector<ParticipantGuess> g = processing::LocalParticipantIdentifier::identifyNow(req);
    QCOMPARE(g.size(), 1);
    QCOMPARE(g[0].personId, anna);
    QCOMPARE(g[0].displayName, QStringLiteral("Anna"));
    QVERIFY(g[0].score > 0.9);
    QCOMPARE(g[0].trackId, QStringLiteral("loopback"));
    QCOMPARE(g[0].windows, 7);
    QCOMPARE(g[0].sampleRef, p.sampleRef);
    // Max over Anna's alpha prints (not the mean), mean over the two shared models.
    const double alpha = library::VoiceprintStore::cosineSimilarity(p.vectors[0].vector, unit(4, 0));
    QCOMPARE(processing::LocalParticipantIdentifier::score(p, req.candidates[1]), (alpha + 1.0) / 2.0);
    // No shared model -> -1.
    QCOMPARE(processing::LocalParticipantIdentifier::score(p, { "X", { print("gamma", unit(4, 0)) } }), -1.0);

    // Above-threshold only.
    req.threshold = 0.995;
    QVERIFY(processing::LocalParticipantIdentifier::identifyNow(req)[0].personId.isNull());
    QVERIFY(processing::LocalParticipantIdentifier::identifyNow(req)[0].displayName.isEmpty());

    // Through the port.
    processing::LocalParticipantIdentifier id;
    req.threshold = -1.0;
    providers::IdentifyJob* job = id.identify(req);
    QVERIFY(waitDone(job));
    QCOMPARE(job->result()[0].personId, anna);
}

void CapabilityPortsTest::llmSummarizer_quick()
{
    ScriptedChat chat;
    chat.script = [](const providers::LlmRequest&) {
        return QStringLiteral("TOPICS\n### [00:00-02:30] Migráció\n- Az állapotot egyeztették.\nSUMMARY\n"
                              R"({"execSummary":"Összegzés.","decisions":["Döntés A"],"actionItems":[]})");
    };
    providers::ProviderConfig cfg;
    cfg.model = QStringLiteral("cfg-model");
    processing::LlmSummarizer s(&chat, cfg);
    providers::SummarizationRequest req;
    req.transcript = shortTranscript();
    req.language = QStringLiteral("magyar");
    req.contextNote = QStringLiteral("Heti egyeztetés");
    providers::SummarizationJob* job = s.summarize(req);
    QVERIFY(waitDone(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    QCOMPARE(chat.requests.size(), 1);
    QCOMPARE(chat.requests[0].model, QStringLiteral("cfg-model"));   // the config's model
    QCOMPARE(job->result().mode, providers::SummarizationMode::Quick);
    QCOMPARE(job->result().summary.execSummary, QStringLiteral("Összegzés."));
    QCOMPARE(job->result().summary.decisions, QStringList{ QStringLiteral("Döntés A") });
    QVERIFY(!job->result().markdown.isEmpty());

    // A failing chat: Service error, no partial result.
    chat.script = [](const providers::LlmRequest&) { return QStringLiteral("nem JSON"); };
    job = s.summarize(req);
    QVERIFY(waitDone(job));
    QCOMPARE(job->error().kind, Error::Kind::Service);

    // No chat provider: Unsupported.
    processing::LlmSummarizer none(nullptr, cfg);
    job = none.summarize(req);
    QVERIFY(waitDone(job));
    QCOMPARE(job->error().kind, Error::Kind::Unsupported);
}

void CapabilityPortsTest::llmSummarizer_complex()
{
    ScriptedChat chat;
    chat.script = [](const providers::LlmRequest& r) {
        const QString user = r.messages.value(1).content;
        static const QRegularExpression topic(QStringLiteral("(?:ELEMZENDŐ TÉMA|TOPIC TO ANALYSE): (.*)"));
        const QRegularExpressionMatch m = topic.match(user);
        if (m.hasMatch())
            return QStringLiteral("A(z) %1 témát egyeztették.\n\n## Döntések\n- Döntés: %1\n\n## Teendők\n- Teszt — Bence")
                .arg(m.captured(1));
        if (user.contains(QStringLiteral("Témánkénti elemzések:")) || user.contains(QStringLiteral("Analyses per topic:")))
            return QStringLiteral("A csapat két témát egyeztetett.\n\n## Teendők\n- Teszt megírása — Bence");
        return QStringLiteral("## Migráció\nA migráció állapota.\n\n## Tesztelés\nKi írja a teszteket.");
    };
    processing::LlmSummarizer s(&chat, providers::ProviderConfig{});
    providers::SummarizationRequest req;
    req.mode = providers::SummarizationMode::Complex;
    req.transcript = shortTranscript();
    req.language = QStringLiteral("magyar");
    providers::SummarizationJob* job = s.summarize(req);
    QSignalSpy progress(job, &providers::CapabilityJob::progress);
    QVERIFY(waitDone(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    const providers::SummarizationResult r = job->result();
    QCOMPARE(r.mode, providers::SummarizationMode::Complex);
    QCOMPARE(r.topics.size(), 2);
    QCOMPARE(r.analyses.size(), 2);
    QCOMPARE(r.analyses[1].topicId, r.topics[1].id);
    QCOMPARE(chat.requests.size(), 4);   // topics + 2 analyses + reduce, in order
    QVERIFY(r.summary.execSummary.contains(QStringLiteral("két témát")));
    QCOMPARE(r.summary.decisions.size(), 2);   // the union of the analyses'
    QCOMPARE(progress.count(), 2);
}

void CapabilityPortsTest::llmTagSuggester_overChat()
{
    QTemporaryDir dir;
    library::MeetingRepository store(dir.filePath("rec"), dir.filePath("meta"));
    const tagsfixture::Library lib = tagsfixture::buildLibrary(store);
    library::TagService tags(&store, dir.filePath("meta/tags.json"));
    const Tag nordvik = tags.addTag(lib.nordvik1, QStringLiteral("Nordvik"));
    tags.addTag(lib.museum1, QStringLiteral("MuseumPlus"));

    ScriptedChat chat;
    chat.script = [](const providers::LlmRequest&) { return QStringLiteral("pick: 1\nnew: Raktárköltözés"); };
    processing::LlmTagSuggestionProvider p(&tags, &chat, providers::ProviderConfig{});
    providers::TagSuggestRequest req;
    req.meetingId = lib.nordvik2;
    req.summaryText = QStringLiteral("A Nordvik raktár költözése csúszik.");
    TagSuggestion seed;
    seed.tagId = nordvik.id;
    seed.name = nordvik.name;
    req.seeds = { seed };
    providers::TagSuggestJob* job = p.suggest(req);
    QVERIFY(waitDone(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    QCOMPARE(chat.requests.size(), 1);
    QVERIFY(chat.requests[0].messages[0].content.contains(QStringLiteral("pick:")));   // the built-in prompt
    QStringList names;
    for (const TagSuggestion& t : job->result()) names << t.name;
    QVERIFY2(names.contains(QStringLiteral("Nordvik")), qPrintable(names.join(',')));
    QVERIFY(names.contains(QStringLiteral("Raktárköltözés")));
}

void CapabilityPortsTest::similarityTagSuggester()
{
    QTemporaryDir dir;
    library::MeetingRepository store(dir.filePath("rec"), dir.filePath("meta"));
    const tagsfixture::Library lib = tagsfixture::buildLibrary(store);
    library::TagService tags(&store, dir.filePath("meta/tags.json"));
    library::MeetingProfiles profiles(&store);
    tags.setProfiles(&profiles);
    QSignalSpy idle(&profiles, &library::MeetingProfiles::idle);
    profiles.ensureBuilt();
    if (!profiles.isIdle()) QVERIFY(idle.wait(10000));
    tags.addTag(lib.nordvik1, QStringLiteral("Nordvik"));
    tags.addTag(lib.museum1, QStringLiteral("MuseumPlus"));

    providers::ProviderRegistry reg;
    library::registerInto(reg, &tags, &profiles);
    const providers::ProviderDescriptor d = reg.descriptor(providers::CapabilityKind::TagSuggestion, "similarity");
    QCOMPARE(d.origin, providers::Origin::Local);
    QVERIFY(!d.traits.dataLeavesDevice);
    std::unique_ptr<providers::ITagSuggester> s(
        reg.create<providers::ITagSuggester>("similarity", providers::ProviderContext{}, providers::ProviderConfig{}, nullptr));
    QVERIFY(s);
    providers::TagSuggestRequest req;
    req.meetingId = lib.nordvik2;
    providers::TagSuggestJob* job = s->suggest(req);
    QVERIFY(waitDone(job));
    QString nordvikId;
    for (const TagSuggestion& t : job->result())
        if (t.name == QLatin1String("Nordvik")) nordvikId = t.tagId;
    QVERIFY(!nordvikId.isEmpty());
    // A tag already on the meeting is not suggested again.
    req.currentTagIds = { nordvikId };
    job = s->suggest(req);
    QVERIFY(waitDone(job));
    for (const TagSuggestion& t : job->result()) QVERIFY(t.name != QLatin1String("Nordvik"));
}

void CapabilityPortsTest::localVoiceCatalog()
{
    QTemporaryDir dir;
    const QString models = dir.filePath("models");
    QDir().mkpath(models);
    // "have" exists; "net" is downloadable from a local file:// source.
    processing::VoiceModelSpec have = spec("have");
    { QFile f(QDir(models).filePath(have.fileName)); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("model"); }
    const QString src = dir.filePath("src/net.onnx");
    QDir().mkpath(dir.filePath("src"));
    { QFile f(src); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("0123456789"); }
    processing::VoiceModelSpec net = spec("net");
    net.downloadUrl = QUrl::fromLocalFile(src).toString();
    net.sizeBytes = 10;

    processing::LocalVoiceModelCatalog cat(models, QString(), { have, net });
    cat.downloader()->setReachableOverride(true);

    providers::ModelListJob* list = cat.list(providers::CapabilityKind::VoiceEmbedding);
    QVERIFY(waitDone(list));
    QCOMPARE(list->result().size(), 2);
    QVERIFY(list->result()[0].available);
    QVERIFY(!list->result()[1].available);
    QCOMPARE(list->result()[1].origin, providers::Origin::Local);
    providers::ModelListJob* other = cat.list(providers::CapabilityKind::Chat);
    QVERIFY(waitDone(other));
    QVERIFY(other->result().isEmpty());

    providers::ModelReadyJob* ready = cat.ensureAvailable(QStringLiteral("have"));
    QVERIFY(waitDone(ready));
    QVERIFY(ready->result().isLocal());
    QCOMPARE(QFileInfo(ready->result().localPath).fileName(), have.fileName);

    qint64 lastDone = -1;
    ready = cat.ensureAvailable(QStringLiteral("net"), [&lastDone](qint64 d, qint64) { lastDone = d; });
    QVERIFY(waitDone(ready, 10000));
    QVERIFY2(!ready->error().isError(), qPrintable(ready->error().detail));
    QVERIFY(QFileInfo::exists(ready->result().localPath));
    QCOMPARE(QFileInfo(ready->result().localPath).size(), qint64(10));
    QVERIFY(lastDone >= 0);

    ready = cat.ensureAvailable(QStringLiteral("nope"));
    QVERIFY(waitDone(ready));
    QCOMPARE(ready->error().kind, Error::Kind::Input);
}

void CapabilityPortsTest::registeredDescriptors()
{
    providers::ProviderRegistry reg;
    providers::registerBuiltinProviders(reg);
    QTemporaryDir dir;
    library::MeetingRepository store(dir.filePath("rec"), dir.filePath("meta"));
    library::TagService tags(&store, dir.filePath("meta/tags.json"));
    processing::ProcessingProviderDeps deps;
    deps.voiceSet = []() { return processing::VoiceEmbedderSet({ { spec("alpha"), "/m/a" } }, fakeLoader()); };
    deps.modelsDir = dir.filePath("models");
    deps.tags = &tags;
    processing::registerInto(reg, deps);

    using K = providers::CapabilityKind;
    const auto origin = [&reg](K k, const QString& id) { return reg.descriptor(k, id).origin; };
    QCOMPARE(origin(K::VoiceEmbedding, "onnx"), providers::Origin::Local);
    QCOMPARE(reg.descriptor(K::VoiceEmbedding, "onnx").catalogRef, QStringLiteral("local-voice-models"));
    QVERIFY(reg.descriptor(K::VoiceEmbedding, "onnx").traits.inputs.contains(providers::InputKind::Pcm));
    QVERIFY(reg.descriptor(K::VoiceEmbedding, "onnx").traits.needsModelFile);
    QCOMPARE(origin(K::ParticipantIdentification, "local"), providers::Origin::Local);
    QCOMPARE(origin(K::Summarization, "llm"), providers::Origin::Local);
    QCOMPARE(origin(K::TagSuggestion, "llm"), providers::Origin::Local);
    QCOMPARE(origin(K::ModelCatalog, "local-voice-models"), providers::Origin::Local);

    // The LLM-based providers take their chat provider from the same registry, by the config's type.
    providers::ProviderConfig cfg;
    cfg.type = QStringLiteral("openai-compat");
    cfg.baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
    QObject owner;
    QVERIFY(reg.create<providers::ISummarizer>("llm", providers::ProviderContext{}, cfg, &owner));
    QVERIFY(reg.create<providers::ITagSuggester>("llm", providers::ProviderContext{}, cfg, &owner));
    cfg.type = QStringLiteral("no-such-chat");
    QVERIFY(!reg.create<providers::ISummarizer>("llm", providers::ProviderContext{}, cfg, &owner));
    std::unique_ptr<providers::IVoiceEmbedder> v(
        reg.create<providers::IVoiceEmbedder>("onnx", providers::ProviderContext{}, providers::ProviderConfig{}, nullptr));
    QCOMPARE(v->defaultModelIds(), QStringList{ QStringLiteral("alpha") });
}

QTEST_GUILESS_MAIN(CapabilityPortsTest)
#include "test_capability_ports.moc"
