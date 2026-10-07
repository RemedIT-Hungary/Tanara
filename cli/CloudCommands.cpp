// tanara-cli — Tanara Cloud parancsok (headless; a GUI K-képernyőinek CLI-megfelelői).
//
//   cloud status                         fiók, egyenleg (ÁFA-jelöléssel), ≈ óra, ÁSZF, notice-ok
//   cloud login                          device flow (a kódot és a címet kiírja, majd vár)
//   cloud key <api_key>                  kézi API-kulcs (a dashboardról, W-14)
//   cloud logout                         kijelentkezés (a helyi kulcs mindenképp törlődik)
//   cloud use [cloud|byo]                az átírás + összefoglaló szolgáltatója
//   cloud tier <stt|llm> <fast|accurate>  szint; cloud lang <kód|auto>
//   cloud models                         katalógus (tier / Expert, ár, diarizáció)
//   cloud estimate <id> [transcribe|summarize] [--mode quick|complex]
//   cloud accept-terms [verzió]
//   cloud pending                        félbemaradt átírások visszaírás-ellenőrzése
//   cloud waitlist <email> --consent [--use-case meetings|interviews|audio_files|other] [--langs hu,en]
//
// A szövegek a K-06 / K-09 / K-10 szövegeinek angol változatai (describeCloudError). A CLI
// kimenete angol, fordítás nélkül — lásd cli/README.md.
#include "CloudCommands.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/store/MeetingStore.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

using namespace tanara;

namespace {
QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }
// A CLI kimenete mindig angol (lásd main.cpp) — a pénz- és időformátum is.
QString lang() { return QStringLiteral("en"); }
QString money(const Money& m, MoneyStyle st = MoneyStyle::Balance) { return formatMoney(m, st, lang()); }

// Egy jel megvárása (timeout ms; true = megjött).
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

int fail(const CloudError& e, const Money& charged = {})
{
    err() << describeCloudError(e, charged, lang()) << "\n";
    err().flush();
    return 2;
}

void printAccount(const AccountInfo& a, CloudAccount* acc)
{
    out() << QStringLiteral("Signed in as %1").arg(a.email.isEmpty() ? acc->email() : a.email) << "\n"
          << QStringLiteral("Balance: %1 (%2)").arg(money(a.balance), vatLabel(a.vatMode)) << "\n"
          << QStringLiteral("≈ %1 Accurate · ≈ %2 Fast transcription").arg(formatHours(a.hoursAccurate, lang()), formatHours(a.hoursFast, lang())) << "\n";
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
    for (const Notice& n : a.notices) out() << "[" << n.level << "] " << n.message << "\n";
    out() << QStringLiteral("My account on the web: %1").arg(a.dashboardUrl) << "\n";
    if (acc->clientTooOld()) out() << QStringLiteral("Tanara Cloud requires an update (%1 or newer).").arg(acc->minClient()) << "\n";
}

void setProviders(AppController& app, bool cloud)
{
    AppSettings s = app.settings()->settings();
    if (cloud) {
        s.sttProviderId = cloud::ProviderId;
        s.llmProviderId = cloud::ProviderId;
        s.sttConfigs[cloud::ProviderId].type = cloud::ProviderId;
        s.llmConfigs[cloud::ProviderId].type = cloud::ProviderId;
    } else {
        s.sttProviderId = QStringLiteral("soniox");
        s.llmProviderId = QStringLiteral("openai-compat");
    }
    app.settings()->setSettings(s);
}
} // namespace

bool printCloudEstimate(AppController& app, const QString& meetingId, const QString& task,
                        const QString& mode, bool* enoughOut)
{
    EstimateResult r; CloudError e; bool done = false;
    app.cloud()->estimate(app.makeEstimateRequest(meetingId, task, mode),
                          [&](const EstimateResult& er, const CloudError& ce) { r = er; e = ce; done = true; });
    QEventLoop loop;
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (done) loop.quit(); });
    poll.start(20);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!done) { err() << QStringLiteral("The estimate did not arrive.") << "\n"; return false; }
    if (e.isError()) { fail(e); return false; }
    const Meeting m = app.store()->load(meetingId);
    if (r.low.micros == r.high.micros)
        out() << QStringLiteral("Estimated cost: ≈ %1").arg(money(r.estimate, MoneyStyle::Charge)) << "\n";
    else
        out() << QStringLiteral("Estimated cost: ≈ %1 (%2 – %3)").arg(money(r.estimate, MoneyStyle::Charge),
                                                           money(r.low, MoneyStyle::Charge), money(r.high, MoneyStyle::Charge)) << "\n";
    for (const EstimateLine& l : r.breakdown) {
        const QString tier = l.tier == QLatin1String("fast") ? QStringLiteral("Fast") : l.tier == QLatin1String("accurate") ? QStringLiteral("Accurate") : l.model;
        const QString label = l.item == QLatin1String("stt")
            ? QStringLiteral("Transcription (%1), %2").arg(tier, formatDuration(m.durationMs))
            : QStringLiteral("Summary (%1, %2)").arg(tier, l.summaryMode == QLatin1String("complex") ? QStringLiteral("complex") : QStringLiteral("quick"));
        out() << "  " << label << " — " << (l.exact ? money(l.amount, MoneyStyle::Charge)
                                                     : QStringLiteral("about %1").arg(money(l.amount, MoneyStyle::Charge)))
              << "  [" << (l.exact ? QStringLiteral("exact") : QStringLiteral("estimated")) << "]\n";
    }
    switch (estimateLevel(r)) {
    case EstimateLevel::Enough:
        out() << QStringLiteral("Your balance: %1 — about %2 left afterwards").arg(money(r.balance), money(r.balanceAfter)) << "\n"; break;
    case EstimateLevel::LittleReserve:
        out() << QStringLiteral("Your balance: %1").arg(money(r.balance)) << "\n"
              << QStringLiteral("Probably enough, but with little margin. If it runs out midway, you pay only for the finished parts and can continue after topping up.") << "\n";
        break;
    case EstimateLevel::NotEnough:
        out() << QStringLiteral("This needs about %1; your balance is %2.").arg(money(r.estimate, MoneyStyle::Charge), money(r.balance)) << "\n"
              << QStringLiteral("Top up to start. Nothing was charged.") << "\n";
        break;
    }
    out() << vatLabel(r.vatMode) << "\n";
    out().flush();
    if (enoughOut) *enoughOut = estimateLevel(r) != EstimateLevel::NotEnough;
    return true;
}

int runCloudCommand(AppController& app, const QStringList& args)
{
    CloudAccount* acc = app.cloud();
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
        QEventLoop loop; CloudError e; bool ok = false;
        QObject::connect(acc, &CloudAccount::accountUpdated, &loop, [&]() { ok = true; loop.quit(); });
        QObject::connect(acc, &CloudAccount::accountFailed, &loop, [&](const CloudError& ce) { e = ce; loop.quit(); });
        acc->refreshAccount();
        loop.exec();
        if (!ok) return fail(e);
        printAccount(acc->account(), acc);
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("login")) {
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &CloudAccount::deviceCodeReady, &loop, [&](const DeviceCode& c) {
            out() << QStringLiteral("Open this address in your browser and approve the code:") << "\n  " << c.verificationUriComplete << "\n"
                  << QStringLiteral("Code: %1 (expires in %2 min)").arg(c.userCode).arg(c.expiresIn / 60) << "\n";
            out().flush();
        });
        QObject::connect(acc, &CloudAccount::deviceFlowSucceeded, &loop, [&](const QString& email) {
            out() << QStringLiteral("Signed in: %1").arg(email) << "\n"; rc = 0;
            // a fiók és a katalógus betöltésére még várunk
            QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        });
        QObject::connect(acc, &CloudAccount::deviceFlowFailed, &loop, [&](const CloudError& e) {
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
        waitFor(acc, &CloudAccount::loggedOut, 10000);
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
        AppSettings s = app.settings()->settings();
        if (kind == QLatin1String("stt")) { s.cloudSttTier = tier; s.cloudSttModel.clear(); }
        else { s.cloudLlmTier = tier; s.cloudLlmModel.clear(); }
        app.settings()->setSettings(s);
        out() << QStringLiteral("Tier set: %1 = %2").arg(kind, tier) << "\n";
        return 0;
    }
    if (sub == QLatin1String("lang")) {
        AppSettings s = app.settings()->settings();
        const QString code = args.value(3);
        s.languageHints = code.isEmpty() || code == QLatin1String("auto") ? QStringList() : QStringList{ code };
        app.settings()->setSettings(s);
        out() << QStringLiteral("Meeting language: %1").arg(s.languageHints.isEmpty() ? QStringLiteral("automatic") : code) << "\n";
        return 0;
    }
    if (sub == QLatin1String("models")) {
        if (!needLogin()) return 1;
        QEventLoop loop; CloudError e; bool ok = false;
        QObject::connect(acc, &CloudAccount::modelsUpdated, &loop, [&]() { ok = true; loop.quit(); });
        QObject::connect(acc, &CloudAccount::modelsFailed, &loop, [&](const CloudError& ce) { e = ce; loop.quit(); });
        acc->fetchModels();
        loop.exec();
        if (!ok) return fail(e);
        const QString hint = app.settings()->settings().languageHints.value(0);
        for (const CloudModel& m : acc->models()) {
            const QString price = m.kind == QLatin1String("stt")
                ? QStringLiteral("%1 / hour").arg(money(m.perHour, MoneyStyle::Charge))
                : QStringLiteral("%1 / 1M input tokens").arg(money(m.perMInput, MoneyStyle::Charge));
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
            waitFor(acc, &CloudAccount::accountUpdated, 10000);
            const TermsStatus ts = acc->account().terms;
            version = ts.needsAcceptance() ? ts.currentVersion : ts.upcomingVersion;
        }
        if (version.isEmpty()) { out() << QStringLiteral("There is no Terms version to accept.") << "\n"; return 0; }
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &CloudAccount::termsAccepted, &loop, [&](const TermsStatus& ts) {
            out() << QStringLiteral("You accepted Terms version %1.").arg(ts.acceptedVersion) << "\n"; rc = 0; loop.quit(); });
        QObject::connect(acc, &CloudAccount::termsFailed, &loop, [&](const CloudError& e) { rc = fail(e); loop.quit(); });
        acc->acceptTerms(version);
        loop.exec();
        return rc;
    }
    if (sub == QLatin1String("pending")) {
        if (!needLogin()) return 1;
        const QStringList ids = acc->pendingTranscriptions();
        out() << QStringLiteral("Pending transcriptions: %1").arg(ids.isEmpty() ? QStringLiteral("—") : ids.join(QStringLiteral(", "))) << "\n";
        QObject::connect(acc, &CloudAccount::previousTranscriptionRefunded, acc, [](const Money& refund, const Money& bal) {
            out() << QStringLiteral("Your previous transcription failed on the provider side. We refunded the %1 charge. Balance: %2.")
                         .arg(money(refund, MoneyStyle::Charge), money(bal)) << "\n";
        });
        acc->checkPendingTranscriptions();
        QEventLoop loop; QTimer::singleShot(3000, &loop, &QEventLoop::quit); loop.exec();
        out() << QStringLiteral("Remaining pending transcriptions: %1").arg(acc->pendingTranscriptions().size()) << "\n";
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("waitlist")) {
        if (!app.cloudTeaser() && !app.cloudLive()) { err() << QStringLiteral("The Tanara Cloud waitlist is disabled in this build.") << "\n"; return 1; }
        WaitlistSignup s;
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
        QObject::connect(acc, &CloudAccount::waitlistJoined, &loop, [&]() {
            out() << QStringLiteral("We sent you a confirmation e-mail.") << "\n"; rc = 0;
            AppSettings st = app.settings()->settings(); st.waitlistEmail = s.email.trimmed(); app.settings()->setSettings(st);
            loop.quit(); });
        QObject::connect(acc, &CloudAccount::waitlistFailed, &loop, [&](const CloudError& e) {
            if (e.kind == CloudErrorKind::DisposableEmail) { err() << QStringLiteral("Disposable e-mail addresses are not accepted.") << "\n"; rc = 2; }
            else rc = fail(e);
            loop.quit(); });
        acc->joinWaitlist(s);
        loop.exec();
        return rc;
    }
    err() << QStringLiteral("Unknown cloud command. Usage: cloud status|login|key|logout|use|tier|lang|models|estimate|accept-terms|pending|waitlist") << "\n";
    return 1;
}
