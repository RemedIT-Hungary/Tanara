// tanara-cli: Tanara Cloud commands (headless; CLI counterparts of the GUI cloud screens).
//
//   cloud status                         account, balance (with VAT note), approx. hours, terms, notices
//   cloud login                          device flow (prints the code and URL, then waits)
//   cloud key <api_key>                  set an API key by hand (from the dashboard)
//   cloud logout                         sign out (the local key is always deleted)
//   cloud use [cloud|byo]                provider for transcription + summary
//   cloud tier <stt|llm> <fast|accurate>  tier; cloud lang <code|auto>
//   cloud models                         catalog (tier / Expert, price, diarization)
//   cloud estimate <id> [transcribe|summarize] [--mode quick|complex]
//   cloud accept-terms [version]
//   cloud pending                        check pending transcriptions for write-back
//   cloud waitlist <email> --consent [--use-case meetings|interviews|audio_files|other] [--langs hu,en]
//
// The texts are the English versions of the GUI cloud strings (describeCloudError). CLI
// output is English and untranslated; see cli/README.md.
#include "CloudCommands.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

using namespace tanara;

namespace {
QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }
// CLI output is always English (see main.cpp), including money and time formats.
QString lang() { return QStringLiteral("en"); }
QString money(const cloud::Money& m, cloud::MoneyStyle st = cloud::MoneyStyle::Balance) { return formatMoney(m, st, lang()); }

// Waits for a signal (timeout in ms; true = it arrived).
template <typename Sender, typename Signal>
bool waitFor(Sender* sender, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool got = false;
    auto c = QObject::connect(sender, signal, &loop, [&]() { got = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return got;
}

int fail(const cloud::CloudError& e, const cloud::Money& charged = {})
{
    err() << describeCloudError(e, charged, lang()) << "\n";
    err().flush();
    return 2;
}

void printAccount(const cloud::AccountInfo& a, cloud::CloudAccount* acc)
{
    out() << QStringLiteral("Signed in as %1").arg(a.email.isEmpty() ? acc->email() : a.email) << "\n"
          << QStringLiteral("Balance: %1 (%2)").arg(money(a.balance), cloud::vatLabel(a.vatMode)) << "\n"
          << QStringLiteral("≈ %1 Accurate · ≈ %2 Fast transcription").arg(cloud::formatHours(a.hoursAccurate, lang()), cloud::formatHours(a.hoursFast, lang())) << "\n";
    if (a.balanceEmpty) out() << QStringLiteral("Your balance is used up. Tanara Cloud processing is paused until you top up; your own-keys mode keeps working.") << "\n";
    else if (a.lowBalance) out() << QStringLiteral("Your balance is low: %1.").arg(money(a.balance)) << "\n";
    out() << QStringLiteral("Trial balance: %1 · online top-up: %2").arg(a.trial, a.topupAvailable ? QStringLiteral("available") : QStringLiteral("not yet (contact us)")) << "\n";
    out() << QStringLiteral("Terms: accepted %1, in effect %2").arg(a.terms.acceptedVersion.isEmpty() ? QStringLiteral("—") : a.terms.acceptedVersion,
                                                      a.terms.currentVersion) << "\n";
    if (a.terms.needsAcceptance())
        out() << QStringLiteral("Accept the new Terms to continue processing: tanara-cli cloud accept-terms") << "\n";
    else if (a.terms.needsEarlyAcceptance())
        out() << QStringLiteral("New Terms (%1), effective %2 — accept with: tanara-cli cloud accept-terms").arg(a.terms.upcomingVersion,
                     a.terms.upcomingEffectiveFrom.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))) << "\n";
    for (const cloud::Notice& n : a.notices) out() << "[" << n.level << "] " << n.message << "\n";
    out() << QStringLiteral("My account on the web: %1").arg(a.dashboardUrl) << "\n";
    if (acc->clientTooOld()) out() << QStringLiteral("Tanara Cloud requires an update (%1 or newer).").arg(acc->minClient()) << "\n";
}

void setProviders(app::AppController& app, bool cloud)
{
    tanara::app::AppSettings s = app.settings()->settings();
    if (cloud) {
        s.providers.stt.selectedId = cloud::ProviderId;
        s.providers.llm.selectedId = cloud::ProviderId;
        s.providers.stt.configs[cloud::ProviderId].type = cloud::ProviderId;
        s.providers.llm.configs[cloud::ProviderId].type = cloud::ProviderId;
    } else {
        s.providers.stt.selectedId = QStringLiteral("soniox");
        s.providers.llm.selectedId = QStringLiteral("openai-compat");
    }
    app.settings()->setSettings(s);
}
} // namespace

bool printCloudEstimate(app::AppController& app, const QString& meetingId, const QString& task,
                        const QString& mode, bool* enoughOut)
{
    cloud::EstimateResult r; cloud::CloudError e; bool done = false;
    app.cloud()->estimate(app.makeEstimateRequest(meetingId, task, mode),
                          [&](const cloud::EstimateResult& er, const cloud::CloudError& ce) { r = er; e = ce; done = true; });
    QEventLoop loop;
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (done) loop.quit(); });
    poll.start(20);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!done) { err() << QStringLiteral("The estimate did not arrive.") << "\n"; return false; }
    if (e.isError()) { fail(e); return false; }
    const Meeting m = app.store()->load(meetingId);
    if (r.low.micros == r.high.micros)
        out() << QStringLiteral("Estimated cost: ≈ %1").arg(money(r.estimate, cloud::MoneyStyle::Charge)) << "\n";
    else
        out() << QStringLiteral("Estimated cost: ≈ %1 (%2 – %3)").arg(money(r.estimate, cloud::MoneyStyle::Charge),
                                                           money(r.low, cloud::MoneyStyle::Charge), money(r.high, cloud::MoneyStyle::Charge)) << "\n";
    for (const cloud::EstimateLine& l : r.breakdown) {
        const QString tier = l.tier == QLatin1String("fast") ? QStringLiteral("Fast") : l.tier == QLatin1String("accurate") ? QStringLiteral("Accurate") : l.model;
        const QString label = l.item == QLatin1String("stt")
            ? QStringLiteral("Transcription (%1), %2").arg(tier, cloud::formatDuration(m.durationMs))
            : QStringLiteral("Summary (%1, %2)").arg(tier, l.summaryMode == QLatin1String("complex") ? QStringLiteral("complex") : QStringLiteral("quick"));
        out() << "  " << label << " — " << (l.exact ? money(l.amount, cloud::MoneyStyle::Charge)
                                                     : QStringLiteral("about %1").arg(money(l.amount, cloud::MoneyStyle::Charge)))
              << "  [" << (l.exact ? QStringLiteral("exact") : QStringLiteral("estimated")) << "]\n";
    }
    switch (estimateLevel(r)) {
    case cloud::EstimateLevel::Enough:
        out() << QStringLiteral("Your balance: %1 — about %2 left afterwards").arg(money(r.balance), money(r.balanceAfter)) << "\n"; break;
    case cloud::EstimateLevel::LittleReserve:
        out() << QStringLiteral("Your balance: %1").arg(money(r.balance)) << "\n"
              << QStringLiteral("Probably enough, but with little margin. If it runs out midway, you pay only for the finished parts and can continue after topping up.") << "\n";
        break;
    case cloud::EstimateLevel::NotEnough:
        out() << QStringLiteral("This needs about %1; your balance is %2.").arg(money(r.estimate, cloud::MoneyStyle::Charge), money(r.balance)) << "\n"
              << QStringLiteral("Top up to start. Nothing was charged.") << "\n";
        break;
    }
    out() << cloud::vatLabel(r.vatMode) << "\n";
    out().flush();
    if (enoughOut) *enoughOut = estimateLevel(r) != cloud::EstimateLevel::NotEnough;
    return true;
}

int runCloudCommand(app::AppController& app, const QStringList& args)
{
    cloud::CloudAccount* acc = app.cloud();
    const QString sub = args.value(2);
    if (!app.cloudLive() && sub != QLatin1String("waitlist")) {
        err() << QStringLiteral("Tanara Cloud mode is off (settings.json: \"cloudEnabled\": true, or TANARA_CLOUD=live).") << "\n";
        return 1;
    }
    auto needLogin = [&]() {
        if (acc->isLoggedIn()) return true;
        err() << QStringLiteral("Sign in to Tanara Cloud: tanara-cli cloud login") << "\n";
        return false;
    };

    if (sub == QLatin1String("status")) {
        if (!needLogin()) return 1;
        QEventLoop loop; cloud::CloudError e; bool ok = false;
        QObject::connect(acc, &cloud::CloudAccount::accountUpdated, &loop, [&]() { ok = true; loop.quit(); });
        QObject::connect(acc, &cloud::CloudAccount::accountFailed, &loop, [&](const cloud::CloudError& ce) { e = ce; loop.quit(); });
        acc->refreshAccount();
        loop.exec();
        if (!ok) return fail(e);
        printAccount(acc->account(), acc);
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("login")) {
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &cloud::CloudAccount::deviceCodeReady, &loop, [&](const cloud::DeviceCode& c) {
            out() << QStringLiteral("Open this address in your browser and approve the code:") << "\n  " << c.verificationUriComplete << "\n"
                  << QStringLiteral("Code: %1 (expires in %2 min)").arg(c.userCode).arg(c.expiresIn / 60) << "\n";
            out().flush();
        });
        QObject::connect(acc, &cloud::CloudAccount::deviceFlowSucceeded, &loop, [&](const QString& email) {
            out() << QStringLiteral("Signed in: %1").arg(email) << "\n"; rc = 0;
            // the account and catalog are still loading
            QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        });
        QObject::connect(acc, &cloud::CloudAccount::deviceFlowFailed, &loop, [&](const cloud::CloudError& e) {
            if (e.code == QLatin1String("access_denied")) err() << QStringLiteral("You denied the connection in the browser.") << "\n";
            else if (e.code == QLatin1String("expired_token")) err() << QStringLiteral("The code expired.") << "\n";
            else fail(e);
            loop.quit();
        });
        acc->startDeviceFlow();
        loop.exec();
        out().flush();
        return rc;
    }
    if (sub == QLatin1String("key")) {
        if (args.value(3).isEmpty()) { err() << QStringLiteral("Usage: cloud key <api_key>") << "\n"; return 1; }
        acc->setManualApiKey(args.value(3));
        out() << QStringLiteral("API key saved.") << "\n";
        return 0;
    }
    if (sub == QLatin1String("logout")) {
        acc->logout();
        waitFor(acc, &cloud::CloudAccount::loggedOut, 10000);
        out() << QStringLiteral("Signed out; the key was deleted on this computer and revoked.") << "\n";
        return 0;
    }
    if (sub == QLatin1String("use")) {
        const bool cloud = args.value(3, QStringLiteral("cloud")) != QLatin1String("byo");
        setProviders(app, cloud);
        out() << (cloud ? QStringLiteral("Transcription and summary: Tanara Cloud.") : QStringLiteral("Transcription and summary: your own keys (BYO).")) << "\n";
        return 0;
    }
    if (sub == QLatin1String("tier")) {
        const QString kind = args.value(3), tier = args.value(4);
        if ((kind != QLatin1String("stt") && kind != QLatin1String("llm")) || (tier != QLatin1String("fast") && tier != QLatin1String("accurate"))) {
            err() << QStringLiteral("Usage: cloud tier <stt|llm> <fast|accurate>") << "\n"; return 1;
        }
        tanara::app::AppSettings s = app.settings()->settings();
        if (kind == QLatin1String("stt")) { s.cloud.sttTier = tier; s.cloud.sttModel.clear(); }
        else { s.cloud.llmTier = tier; s.cloud.llmModel.clear(); }
        app.settings()->setSettings(s);
        out() << QStringLiteral("Tier set: %1 = %2").arg(kind, tier) << "\n";
        return 0;
    }
    if (sub == QLatin1String("lang")) {
        tanara::app::AppSettings s = app.settings()->settings();
        const QString code = args.value(3);
        s.processing.languageHints = code.isEmpty() || code == QLatin1String("auto") ? QStringList() : QStringList{ code };
        app.settings()->setSettings(s);
        out() << QStringLiteral("Meeting language: %1").arg(s.processing.languageHints.isEmpty() ? QStringLiteral("automatic") : code) << "\n";
        return 0;
    }
    if (sub == QLatin1String("models")) {
        if (!needLogin()) return 1;
        QEventLoop loop; cloud::CloudError e; bool ok = false;
        QObject::connect(acc, &cloud::CloudAccount::modelsUpdated, &loop, [&]() { ok = true; loop.quit(); });
        QObject::connect(acc, &cloud::CloudAccount::modelsFailed, &loop, [&](const cloud::CloudError& ce) { e = ce; loop.quit(); });
        acc->fetchModels();
        loop.exec();
        if (!ok) return fail(e);
        const QString hint = app.settings()->settings().processing.languageHints.value(0);
        for (const cloud::CloudModel& m : acc->models()) {
            const QString price = m.kind == QLatin1String("stt")
                ? QStringLiteral("%1 / hour").arg(money(m.perHour, cloud::MoneyStyle::Charge))
                : QStringLiteral("%1 / 1M input tokens").arg(money(m.perMInput, cloud::MoneyStyle::Charge));
            out() << QStringLiteral("  %1  %2  %3  %4%5%6\n").arg(m.id, -32).arg(m.kind, -3)
                         .arg(m.isVirtual ? m.tier : QStringLiteral("expert"), -8).arg(price)
                         .arg(m.kind == QLatin1String("stt") && !m.diarization ? QStringLiteral("  ▲ doesn't separate speakers") : QString())
                         .arg(m.notRecommendedFor(hint) ? QStringLiteral("  ▲ not recommended for this language") : QString());
        }
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("estimate")) {
        if (!needLogin()) return 1;
        const QString id = args.value(3);
        if (id.isEmpty()) { err() << QStringLiteral("Usage: cloud estimate <meetingId> [transcribe|summarize] [--mode quick|complex]") << "\n"; return 1; }
        const QString task = args.value(4).startsWith(QLatin1String("--")) || args.value(4).isEmpty() ? QStringLiteral("transcribe") : args.value(4);
        const int mi = args.indexOf(QStringLiteral("--mode"));
        const QString mode = mi > 0 ? args.value(mi + 1) : QStringLiteral("quick");
        return printCloudEstimate(app, id, task, mode, nullptr) ? 0 : 2;
    }
    if (sub == QLatin1String("accept-terms")) {
        if (!needLogin()) return 1;
        QString version = args.value(3);
        if (version.isEmpty()) {
            acc->refreshAccount();
            waitFor(acc, &cloud::CloudAccount::accountUpdated, 10000);
            const cloud::TermsStatus ts = acc->account().terms;
            version = ts.needsAcceptance() ? ts.currentVersion : ts.upcomingVersion;
        }
        if (version.isEmpty()) { out() << QStringLiteral("There is no Terms version to accept.") << "\n"; return 0; }
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &cloud::CloudAccount::termsAccepted, &loop, [&](const cloud::TermsStatus& ts) {
            out() << QStringLiteral("You accepted Terms version %1.").arg(ts.acceptedVersion) << "\n"; rc = 0; loop.quit(); });
        QObject::connect(acc, &cloud::CloudAccount::termsFailed, &loop, [&](const cloud::CloudError& e) { rc = fail(e); loop.quit(); });
        acc->acceptTerms(version);
        loop.exec();
        return rc;
    }
    if (sub == QLatin1String("pending")) {
        if (!needLogin()) return 1;
        const QStringList ids = acc->pendingTranscriptions();
        out() << QStringLiteral("Pending transcriptions: %1").arg(ids.isEmpty() ? QStringLiteral("—") : ids.join(QStringLiteral(", "))) << "\n";
        QObject::connect(acc, &cloud::CloudAccount::previousTranscriptionRefunded, acc, [](const cloud::Money& refund, const cloud::Money& bal) {
            out() << QStringLiteral("Your previous transcription failed on the provider side. We refunded the %1 charge. Balance: %2.")
                         .arg(money(refund, cloud::MoneyStyle::Charge), money(bal)) << "\n";
        });
        acc->checkPendingTranscriptions();
        QEventLoop loop; QTimer::singleShot(3000, &loop, &QEventLoop::quit); loop.exec();
        out() << QStringLiteral("Remaining pending transcriptions: %1").arg(acc->pendingTranscriptions().size()) << "\n";
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("waitlist")) {
        if (!app.cloudTeaser() && !app.cloudLive()) { err() << QStringLiteral("The Tanara Cloud waitlist is disabled in this build.") << "\n"; return 1; }
        cloud::WaitlistSignup s;
        s.email = args.value(3);
        s.consent = args.contains(QStringLiteral("--consent"));
        s.uiLanguage = lang();
        const int ui = args.indexOf(QStringLiteral("--use-case"));
        if (ui > 0) s.useCase = args.value(ui + 1);
        const int li = args.indexOf(QStringLiteral("--langs"));
        if (li > 0) s.meetingLanguages = args.value(li + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QString bad = validateWaitlist(s);
        if (bad == QLatin1String("email")) { err() << QStringLiteral("Check the e-mail address.") << "\n"; return 1; }
        if (bad == QLatin1String("consent")) {
            err() << QStringLiteral("Signing up needs explicit consent (--consent): “Notify me when Tanara Cloud launches. I can unsubscribe at any time.”") << "\n";
            return 1;
        }
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &cloud::CloudAccount::waitlistJoined, &loop, [&]() {
            out() << QStringLiteral("We sent you a confirmation e-mail.") << "\n"; rc = 0;
            tanara::app::AppSettings st = app.settings()->settings(); st.cloud.waitlistEmail = s.email.trimmed(); app.settings()->setSettings(st);
            loop.quit(); });
        QObject::connect(acc, &cloud::CloudAccount::waitlistFailed, &loop, [&](const cloud::CloudError& e) {
            if (e.kind == cloud::CloudErrorKind::DisposableEmail) { err() << QStringLiteral("Disposable e-mail addresses are not accepted.") << "\n"; rc = 2; }
            else rc = fail(e);
            loop.quit(); });
        acc->joinWaitlist(s);
        loop.exec();
        return rc;
    }
    err() << QStringLiteral("Unknown cloud command. Usage: cloud status|login|key|logout|use|tier|lang|models|estimate|accept-terms|pending|waitlist") << "\n";
    return 1;
}
