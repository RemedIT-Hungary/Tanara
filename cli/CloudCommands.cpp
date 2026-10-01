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
// A szövegek a K-06 / K-09 / K-10 szövegeivel azonosak (describeCloudError).
#include "CloudCommands.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
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
QString lang() { return activeUiLanguage(); }
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
    out() << QCoreApplication::translate("cli", "Bejelentkezve: %1").arg(a.email.isEmpty() ? acc->email() : a.email) << "\n"
          << QCoreApplication::translate("cli", "Egyenleg: %1 (%2)").arg(money(a.balance), vatLabel(a.vatMode)) << "\n"
          << QCoreApplication::translate("cli", "≈ %1 Pontos · ≈ %2 Gyors átírás").arg(formatHours(a.hoursAccurate, lang()), formatHours(a.hoursFast, lang())) << "\n";
    if (a.balanceEmpty) out() << QCoreApplication::translate("cli", "Elfogyott az egyenleged. A Tanara Cloud feldolgozás a feltöltésig szünetel; a saját kulcsos mód továbbra is működik.") << "\n";
    else if (a.lowBalance) out() << QCoreApplication::translate("cli", "Kevés az egyenleged: %1.").arg(money(a.balance)) << "\n";
    out() << QCoreApplication::translate("cli", "Próbaegyenleg: %1 · online feltöltés: %2").arg(a.trial, a.topupAvailable ? QCoreApplication::translate("cli", "van") : QCoreApplication::translate("cli", "nincs (írj nekünk)")) << "\n";
    out() << QCoreApplication::translate("cli", "ÁSZF: elfogadva %1, hatályos %2").arg(a.terms.acceptedVersion.isEmpty() ? QStringLiteral("—") : a.terms.acceptedVersion,
                                                      a.terms.currentVersion) << "\n";
    if (a.terms.needsAcceptance())
        out() << QCoreApplication::translate("cli", "A feldolgozáshoz el kell fogadnod az új ÁSZF-et: tanara-cli cloud accept-terms") << "\n";
    else if (a.terms.needsEarlyAcceptance())
        out() << QCoreApplication::translate("cli", "Új ÁSZF (%1), hatályos: %2 — elfogadás: tanara-cli cloud accept-terms").arg(a.terms.upcomingVersion,
                     a.terms.upcomingEffectiveFrom.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))) << "\n";
    for (const Notice& n : a.notices) out() << "[" << n.level << "] " << n.message << "\n";
    out() << QCoreApplication::translate("cli", "Fiókom a weben: %1").arg(a.dashboardUrl) << "\n";
    if (acc->clientTooOld()) out() << QCoreApplication::translate("cli", "A Tanara Cloudhoz frissítés kell (legalább %1).").arg(acc->minClient()) << "\n";
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
    if (!done) { err() << QCoreApplication::translate("cli", "A becslés nem érkezett meg.") << "\n"; return false; }
    if (e.isError()) { fail(e); return false; }
    const Meeting m = app.store()->load(meetingId);
    if (r.low.micros == r.high.micros)
        out() << QCoreApplication::translate("cli", "Becsült költség: ≈ %1").arg(money(r.estimate, MoneyStyle::Charge)) << "\n";
    else
        out() << QCoreApplication::translate("cli", "Becsült költség: ≈ %1 (%2 – %3 között)").arg(money(r.estimate, MoneyStyle::Charge),
                                                           money(r.low, MoneyStyle::Charge), money(r.high, MoneyStyle::Charge)) << "\n";
    for (const EstimateLine& l : r.breakdown) {
        const QString tier = l.tier == QLatin1String("fast") ? QCoreApplication::translate("cli", "Gyors") : l.tier == QLatin1String("accurate") ? QCoreApplication::translate("cli", "Pontos") : l.model;
        const QString label = l.item == QLatin1String("stt")
            ? QCoreApplication::translate("cli", "Átírás (%1), %2").arg(tier, formatDuration(m.durationMs))
            : QCoreApplication::translate("cli", "Összefoglaló (%1, %2)").arg(tier, l.summaryMode == QLatin1String("complex") ? QCoreApplication::translate("cli", "komplex") : QCoreApplication::translate("cli", "gyors"));
        out() << "  " << label << " — " << (l.exact ? money(l.amount, MoneyStyle::Charge)
                                                     : QCoreApplication::translate("cli", "kb. %1").arg(money(l.amount, MoneyStyle::Charge)))
              << "  [" << (l.exact ? QCoreApplication::translate("cli", "pontos") : QCoreApplication::translate("cli", "becsült")) << "]\n";
    }
    switch (estimateLevel(r)) {
    case EstimateLevel::Enough:
        out() << QCoreApplication::translate("cli", "Egyenleged: %1 — utána kb. %2 marad").arg(money(r.balance), money(r.balanceAfter)) << "\n"; break;
    case EstimateLevel::LittleReserve:
        out() << QCoreApplication::translate("cli", "Egyenleged: %1").arg(money(r.balance)) << "\n"
              << QCoreApplication::translate("cli", "Valószínűleg elég, de kevés tartalék marad. Ha menet közben elfogy, a már elkészült részek díja terhelődik, és feltöltés után folytathatod.") << "\n";
        break;
    case EstimateLevel::NotEnough:
        out() << QCoreApplication::translate("cli", "Ehhez kb. %1 kell, az egyenleged %2.").arg(money(r.estimate, MoneyStyle::Charge), money(r.balance)) << "\n"
              << QCoreApplication::translate("cli", "Az indításhoz tölts fel. Nem terheltünk semmit.") << "\n";
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
        err() << QCoreApplication::translate("cli", "A Tanara Cloud mód nincs bekapcsolva (settings.json: \"cloudEnabled\": true, vagy TANARA_CLOUD=live).") << "\n";
        return 1;
    }
    auto needLogin = [&]() {
        if (acc->isLoggedIn()) return true;
        err() << QCoreApplication::translate("cli", "Jelentkezz be a Tanara Cloudba: tanara-cli cloud login") << "\n";
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
            out() << QCoreApplication::translate("cli", "Nyisd meg ezt a címet a böngészőben, és hagyd jóvá a kódot:") << "\n  " << c.verificationUriComplete << "\n"
                  << QCoreApplication::translate("cli", "Kód: %1 (lejár %2 perc múlva)").arg(c.userCode).arg(c.expiresIn / 60) << "\n";
            out().flush();
        });
        QObject::connect(acc, &CloudAccount::deviceFlowSucceeded, &loop, [&](const QString& email) {
            out() << QCoreApplication::translate("cli", "Sikeres bejelentkezés: %1").arg(email) << "\n"; rc = 0;
            // a fiók és a katalógus betöltésére még várunk
            QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        });
        QObject::connect(acc, &CloudAccount::deviceFlowFailed, &loop, [&](const CloudError& e) {
            if (e.code == QLatin1String("access_denied")) err() << QCoreApplication::translate("cli", "A kapcsolódást elutasítottad a böngészőben.") << "\n";
            else if (e.code == QLatin1String("expired_token")) err() << QCoreApplication::translate("cli", "A kód lejárt.") << "\n";
            else fail(e);
            loop.quit();
        });
        acc->startDeviceFlow();
        loop.exec();
        out().flush();
        return rc;
    }
    if (sub == QLatin1String("key")) {
        if (args.value(3).isEmpty()) { err() << QCoreApplication::translate("cli", "Használat: cloud key <api_kulcs>") << "\n"; return 1; }
        acc->setManualApiKey(args.value(3));
        out() << QCoreApplication::translate("cli", "API-kulcs elmentve.") << "\n";
        return 0;
    }
    if (sub == QLatin1String("logout")) {
        acc->logout();
        waitFor(acc, &CloudAccount::loggedOut, 10000);
        out() << QCoreApplication::translate("cli", "Kijelentkezve; a kulcsot ezen a gépen töröltük és visszavontuk.") << "\n";
        return 0;
    }
    if (sub == QLatin1String("use")) {
        const bool cloud = args.value(3, QStringLiteral("cloud")) != QLatin1String("byo");
        setProviders(app, cloud);
        out() << (cloud ? QCoreApplication::translate("cli", "Átírás és összefoglaló: Tanara Cloud.") : QCoreApplication::translate("cli", "Átírás és összefoglaló: saját kulcsok (BYO).")) << "\n";
        return 0;
    }
    if (sub == QLatin1String("tier")) {
        const QString kind = args.value(3), tier = args.value(4);
        if ((kind != QLatin1String("stt") && kind != QLatin1String("llm")) || (tier != QLatin1String("fast") && tier != QLatin1String("accurate"))) {
            err() << QCoreApplication::translate("cli", "Használat: cloud tier <stt|llm> <fast|accurate>") << "\n"; return 1;
        }
        AppSettings s = app.settings()->settings();
        if (kind == QLatin1String("stt")) { s.cloudSttTier = tier; s.cloudSttModel.clear(); }
        else { s.cloudLlmTier = tier; s.cloudLlmModel.clear(); }
        app.settings()->setSettings(s);
        out() << QCoreApplication::translate("cli", "Szint beállítva: %1 = %2").arg(kind, tier) << "\n";
        return 0;
    }
    if (sub == QLatin1String("lang")) {
        AppSettings s = app.settings()->settings();
        const QString code = args.value(3);
        s.languageHints = code.isEmpty() || code == QLatin1String("auto") ? QStringList() : QStringList{ code };
        app.settings()->setSettings(s);
        out() << QCoreApplication::translate("cli", "A meeting nyelve: %1").arg(s.languageHints.isEmpty() ? QCoreApplication::translate("cli", "automatikus") : code) << "\n";
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
                ? QCoreApplication::translate("cli", "%1 / óra").arg(money(m.perHour, MoneyStyle::Charge))
                : QCoreApplication::translate("cli", "%1 / 1M bemeneti token").arg(money(m.perMInput, MoneyStyle::Charge));
            out() << QStringLiteral("  %1  %2  %3  %4%5%6\n").arg(m.id, -32).arg(m.kind, -3)
                         .arg(m.isVirtual ? m.tier : QStringLiteral("expert"), -8).arg(price)
                         .arg(m.kind == QLatin1String("stt") && !m.diarization ? QCoreApplication::translate("cli", "  ▲ nem különíti el a beszélőket") : QString())
                         .arg(m.notRecommendedFor(hint) ? QCoreApplication::translate("cli", "  ▲ ehhez a nyelvhez nem ajánlott") : QString());
        }
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("estimate")) {
        if (!needLogin()) return 1;
        const QString id = args.value(3);
        if (id.isEmpty()) { err() << QCoreApplication::translate("cli", "Használat: cloud estimate <meetingId> [transcribe|summarize] [--mode quick|complex]") << "\n"; return 1; }
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
        if (version.isEmpty()) { out() << QCoreApplication::translate("cli", "Nincs elfogadandó ÁSZF-verzió.") << "\n"; return 0; }
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &CloudAccount::termsAccepted, &loop, [&](const TermsStatus& ts) {
            out() << QCoreApplication::translate("cli", "Elfogadtad az ÁSZF %1 verzióját.").arg(ts.acceptedVersion) << "\n"; rc = 0; loop.quit(); });
        QObject::connect(acc, &CloudAccount::termsFailed, &loop, [&](const CloudError& e) { rc = fail(e); loop.quit(); });
        acc->acceptTerms(version);
        loop.exec();
        return rc;
    }
    if (sub == QLatin1String("pending")) {
        if (!needLogin()) return 1;
        const QStringList ids = acc->pendingTranscriptions();
        out() << QCoreApplication::translate("cli", "Függő átírások: %1").arg(ids.isEmpty() ? QStringLiteral("—") : ids.join(QStringLiteral(", "))) << "\n";
        QObject::connect(acc, &CloudAccount::previousTranscriptionRefunded, acc, [](const Money& refund, const Money& bal) {
            out() << QCoreApplication::translate("cli", "Az előző átírás a szolgáltató hibája miatt nem sikerült. A díjat (%1) visszaírtuk. Egyenleg: %2.")
                         .arg(money(refund, MoneyStyle::Charge), money(bal)) << "\n";
        });
        acc->checkPendingTranscriptions();
        QEventLoop loop; QTimer::singleShot(3000, &loop, &QEventLoop::quit); loop.exec();
        out() << QCoreApplication::translate("cli", "Hátralévő függő átírások: %1").arg(acc->pendingTranscriptions().size()) << "\n";
        out().flush();
        return 0;
    }
    if (sub == QLatin1String("waitlist")) {
        if (!app.cloudTeaser() && !app.cloudLive()) { err() << QCoreApplication::translate("cli", "A Tanara Cloud várólista ebben a buildben ki van kapcsolva.") << "\n"; return 1; }
        WaitlistSignup s;
        s.email = args.value(3);
        s.consent = args.contains(QStringLiteral("--consent"));
        s.uiLanguage = lang();
        const int ui = args.indexOf(QStringLiteral("--use-case"));
        if (ui > 0) s.useCase = args.value(ui + 1);
        const int li = args.indexOf(QStringLiteral("--langs"));
        if (li > 0) s.meetingLanguages = args.value(li + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QString bad = validateWaitlist(s);
        if (bad == QLatin1String("email")) { err() << QCoreApplication::translate("cli", "Ellenőrizd az e-mail címet.") << "\n"; return 1; }
        if (bad == QLatin1String("consent")) {
            err() << QCoreApplication::translate("cli", "A feliratkozáshoz kifejezett hozzájárulás kell (--consent): „Értesítést kérek a Tanara Cloud indulásáról. Bármikor leiratkozhatok.”") << "\n";
            return 1;
        }
        QEventLoop loop; int rc = 1;
        QObject::connect(acc, &CloudAccount::waitlistJoined, &loop, [&]() {
            out() << QCoreApplication::translate("cli", "Küldtünk egy megerősítő e-mailt.") << "\n"; rc = 0;
            AppSettings st = app.settings()->settings(); st.waitlistEmail = s.email.trimmed(); app.settings()->setSettings(st);
            loop.quit(); });
        QObject::connect(acc, &CloudAccount::waitlistFailed, &loop, [&](const CloudError& e) {
            if (e.kind == CloudErrorKind::DisposableEmail) { err() << QCoreApplication::translate("cli", "Eldobható e-mail címmel nem lehet feliratkozni.") << "\n"; rc = 2; }
            else rc = fail(e);
            loop.quit(); });
        acc->joinWaitlist(s);
        loop.exec();
        return rc;
    }
    err() << QCoreApplication::translate("cli", "Ismeretlen cloud-parancs. Használat: cloud status|login|key|logout|use|tier|lang|models|estimate|accept-terms|pending|waitlist") << "\n";
    return 1;
}
