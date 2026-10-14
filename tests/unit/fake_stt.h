#pragma once
// Fakes for the transcription step's tests: an STT provider whose jobs the test finishes / fails by hand
// (cancel answers with a synchronous failed("cancelled"), like the real providers), and a
// TranscriptionPort that records the runs it handed out. No network.
#include "tanara/processing/stt/TranscriptionPort.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"

#include <QObject>
#include <QPointer>
#include <QVector>

#include <memory>

namespace tanara::testing {

class FakeSttJob final : public providers::SttJob {
public:
    using providers::SttJob::SttJob;
    bool cancelled = false;
    void cancel() override
    {
        cancelled = true;
        emit failed(QStringLiteral("cancelled"));
    }
};

// The jobs of every fake provider instance, in start order.
struct FakeSttLog {
    QVector<QPointer<FakeSttJob>> jobs;
    QVector<providers::SttRequest> requests;
    int providersCreated = 0;
};

class FakeSttProvider final : public QObject, public providers::ISttProvider {
public:
    FakeSttProvider(std::shared_ptr<FakeSttLog> log, QObject* parent) : QObject(parent), m_log(std::move(log)) {}
    QString name() const override { return QStringLiteral("fake"); }
    bool supportsLive() const override { return false; }
    providers::SttJob* transcribe(const providers::SttRequest& req) override
    {
        auto* job = new FakeSttJob(this);
        m_log->jobs << job;
        m_log->requests << req;
        return job;
    }

private:
    std::shared_ptr<FakeSttLog> m_log;
};

inline const QString kFakeSttId = QStringLiteral("fake-stt");

inline void registerFakeStt(providers::ProviderRegistry& reg, const std::shared_ptr<FakeSttLog>& log)
{
    providers::ProviderDescriptor d;
    d.id = kFakeSttId;
    d.displayName = kFakeSttId;
    reg.add<providers::ISttProvider>(d, [log](const providers::ProviderContext&, const providers::ProviderConfig&,
                                              QObject* parent) -> providers::ISttProvider* {
        ++log->providersCreated;
        return new FakeSttProvider(log, parent);
    });
}

// A result with one word per (speaker, text) pair, 300 ms apart.
inline TrackTranscript sttResult(const QVector<QPair<QString, QString>>& words)
{
    TrackTranscript t;
    t.trackId = QStringLiteral("mixdown");
    t.language = QStringLiteral("hu");
    qint64 at = 0;
    for (const auto& w : words) {
        TranscriptToken tok;
        tok.speaker = w.first;
        tok.text = w.second;
        tok.startMs = at;
        tok.endMs = at + 250;
        tok.confidence = 0.9;
        t.tokens << tok;
        at += 300;
    }
    return t;
}

class FakeTranscriptionPort final : public processing::TranscriptionPort {
public:
    struct Handle final : RunHandle { int id = 0; };

    QString providerId = kFakeSttId;
    bool cloud = false;          // hand out a run
    bool diarization = true;
    bool reportFailure = false;  // failRun: the "gateway" reported the error itself
    int runs = 0, finished = 0, failed = 0, cancelled = 0;

    Setup setup(const QString&) override
    {
        Setup s;
        s.providerId = providerId;
        s.config.model = QStringLiteral("fake-model");
        s.diarization = diarization;
        s.languageHints = {QStringLiteral("hu")};
        if (cloud) {
            auto h = std::make_shared<Handle>();
            h->id = ++runs;
            s.run = h;
        }
        return s;
    }
    providers::ProviderContext context(const Run&) const override { return {}; }
    void finishRun(const Run& run) override { if (run) ++finished; }
    bool failRun(const Run& run) override
    {
        if (run) ++failed;
        return run && reportFailure;
    }
    bool runError(const Run&, Error*) const override { return false; }
    void cancelledRun(const Run& run) override { if (run) ++cancelled; }
};

} // namespace tanara::testing
