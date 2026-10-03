#include "SettingsCloudModel.h"

#include "SettingsDialogs.h"
#include "SettingsViewModel.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"

using namespace tanara;

namespace tanara_qml {

SettingsCloudModel::SettingsCloudModel(SettingsViewModel* vm) : QObject(vm), m_vm(vm) {}

QString SettingsCloudModel::lang() const
{
    const QString l = activeUiLanguage();
    return l.isEmpty() ? QStringLiteral("hu") : l;
}

void SettingsCloudModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    m_lastError = {};
    m_waitBusy = false;
    AppController* c = m_vm->controller();
    m_demo = !c;
    if (!c) return;
    CloudAccount* acc = c->cloud();
    if (!acc) return;
    m_connections << connect(acc, &CloudAccount::accountUpdated, this, [this](const AccountInfo&) {
        m_lastError = {};
        emit accountChanged();
    });
    m_connections << connect(acc, &CloudAccount::accountFailed, this, [this](const CloudError& e) {
        m_lastError = e;
        emit accountChanged();
    });
    m_connections << connect(acc, &CloudAccount::balanceChanged, this, &SettingsCloudModel::accountChanged);
    m_connections << connect(acc, &CloudAccount::loggedIn, this, [this] {
        emit accountChanged();
        m_vm->touch();   // a „Szolgáltatások” pöttye a bejelentkezéstől is függ
    });
    m_connections << connect(acc, &CloudAccount::loggedOut, this, [this] {
        m_lastError = {};
        emit accountChanged();
        m_vm->touch();
    });
    m_connections << connect(acc, &CloudAccount::clientTooOldDetected, this, &SettingsCloudModel::accountChanged);
    m_connections << connect(acc, &CloudAccount::termsAccepted, this, &SettingsCloudModel::accountChanged);
    m_connections << connect(acc, &CloudAccount::modelsUpdated, this, &SettingsCloudModel::tiersChanged);
    m_connections << connect(acc, &CloudAccount::waitlistJoined, this, [this](const QString& email) {
        if (!m_waitBusy) return;
        m_waitBusy = false;
        m_waitError.clear();
        // A feliratkozás ténye azonnal mentődik (nem a piszkozat része — már megtörtént).
        if (AppController* app = m_vm->controller()) {
            AppSettings s = app->settings()->settings();
            s.waitlistEmail = email;
            app->settings()->setSettings(s);
        }
        emit waitlistChanged();
    });
    m_connections << connect(acc, &CloudAccount::waitlistFailed, this, [this](const CloudError& e) {
        if (!m_waitBusy) return;
        m_waitBusy = false;
        if (e.kind == CloudErrorKind::DisposableEmail)
            m_waitError = tr("Eldobható e-mail címmel nem lehet feliratkozni. Adj meg egy állandó címet.");
        else if (e.kind == CloudErrorKind::Validation && e.fieldErrors.contains(QStringLiteral("email")))
            m_waitError = tr("Ellenőrizd az e-mail címet.");
        else if (e.kind == CloudErrorKind::Network)
            m_waitError = tr("Nem értük el a szervert. Ellenőrizd az internetkapcsolatot.");
        else
            m_waitError = e.message.isEmpty() ? tr("Nem sikerült a feliratkozás. Próbáld újra később.")
                                              : e.message;
        if (!e.requestId.isEmpty())
            m_waitError += QStringLiteral(" (") + tr("Hibaazonosító: %1").arg(e.requestId) + QLatin1Char(')');
        emit waitlistChanged();
    });
}

// ---- fiók ------------------------------------------------------------------------------

bool SettingsCloudModel::loggedIn() const
{
    if (m_demo) return m_demoLoggedIn;
    AppController* c = m_vm->controller();
    return c && c->cloud() && c->cloud()->isLoggedIn();
}

QString SettingsCloudModel::email() const
{
    if (m_demo) return m_demoLoggedIn ? QStringLiteral("lilla.kovacs@example.com") : QString();
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return {};
    const AccountInfo a = c->cloud()->account();
    return a.email.isEmpty() ? c->cloud()->email() : a.email;
}

QString SettingsCloudModel::balanceText() const
{
    if (m_demo)   // kitalált fiók — a formázás ugyanaz, mint az élő adatnál
        return m_demoLoggedIn ? formatMoney(Money{84'200'000, QStringLiteral("USD")}, MoneyStyle::Balance, lang())
                              : QString();
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return {};
    const AccountInfo a = c->cloud()->account();
    return a.valid ? formatMoney(a.balance, MoneyStyle::Balance, lang()) : QString();
}

QString SettingsCloudModel::balanceState() const
{
    if (m_demo) return QStringLiteral("ok");
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return QStringLiteral("unknown");
    const AccountInfo a = c->cloud()->account();
    if (!a.valid) return QStringLiteral("unknown");
    return a.balanceEmpty ? QStringLiteral("empty") : a.lowBalance ? QStringLiteral("low")
                                                                   : QStringLiteral("ok");
}

QString SettingsCloudModel::hoursText() const
{
    if (m_demo)
        return m_demoLoggedIn ? tr("≈ %1 Pontos · ≈ %2 Gyors átírás")
                                    .arg(formatHours(14.0, lang()), formatHours(56.0, lang()))
                              : QString();
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return {};
    const AccountInfo a = c->cloud()->account();
    if (!a.valid) return {};
    return tr("≈ %1 Pontos · ≈ %2 Gyors átírás")
        .arg(formatHours(a.hoursAccurate, lang()), formatHours(a.hoursFast, lang()));
}

QString SettingsCloudModel::vatText() const
{
    if (m_demo) return QString();
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return {};
    const AccountInfo a = c->cloud()->account();
    return a.valid ? vatLabel(a.vatMode) : QString();
}

QString SettingsCloudModel::topupLabel() const
{
    if (m_demo) return tr("Feltöltés");
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return tr("Feltöltés");
    const AccountInfo a = c->cloud()->account();
    return a.valid && !a.topupAvailable ? tr("Írj nekünk a feltöltéshez") : tr("Feltöltés");
}

QVariantMap SettingsCloudModel::notice() const
{
    auto mk = [](const QString& text, const QString& tone, const QString& action,
                 const QString& label) {
        return QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("tone"), tone},
                           {QStringLiteral("action"), action}, {QStringLiteral("actionLabel"), label}};
    };
    AppController* c = m_vm->controller();
    if (m_demo || !c || !c->cloud()) return {};
    CloudAccount* acc = c->cloud();
    if (acc->clientTooOld())
        return mk(tr("Frissítsd a Tanarát (legalább %1; neked %2 van).").arg(acc->minClient(), libraryVersion()),
                  QStringLiteral("danger"), QStringLiteral("update"), tr("Frissítés"));
    if (!acc->isLoggedIn()) return {};
    const AccountInfo a = acc->account();
    const CloudError& e = m_lastError;
    if (e.kind == CloudErrorKind::Unauthorized)
        return mk(tr("Ezt az eszközt leválasztották. Jelentkezz be újra."), QStringLiteral("danger"),
                  QStringLiteral("login"), tr("Bejelentkezés"));
    if (e.kind == CloudErrorKind::Suspended)
        return mk(e.suspensionReason == QLatin1String("payment_dispute")
                      ? tr("A fiókod fizetési vita miatt fel van függesztve. Részletek a weben.")
                      : tr("A fiókod fel van függesztve. Írj a supportnak."),
                  QStringLiteral("danger"), QStringLiteral("web"), tr("Megnyitás a weben"));
    if (e.isError()) {
        const QString when = acc->accountFetchedAt().isValid()
            ? acc->accountFetchedAt().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("—");
        QString text = a.valid
            ? tr("Az egyenleg most nem elérhető — utolsó ismert egyenleg: %1 (%2).")
                  .arg(formatMoney(a.balance, MoneyStyle::Balance, lang()), when)
            : tr("Az egyenleg most nem elérhető.");
        if (!e.requestId.isEmpty()) text += QLatin1Char(' ') + tr("Hibaazonosító: %1").arg(e.requestId);
        return mk(text, QStringLiteral("info"), QString(), QString());
    }
    if (a.valid && a.balanceEmpty)
        return mk(tr("Elfogyott az egyenleged. A Tanara Cloud feldolgozás a feltöltésig szünetel."),
                  QStringLiteral("danger"), QStringLiteral("topup"),
                  a.topupAvailable ? tr("Feltöltés") : tr("Írj nekünk"));
    if (a.valid && a.lowBalance)
        return mk(tr("Kevés az egyenleged."), QStringLiteral("warn"), QStringLiteral("topup"),
                  a.topupAvailable ? tr("Feltöltés") : tr("Írj nekünk"));
    if (a.trial == QLatin1String("awaiting_email"))
        return mk(tr("A próbaegyenleg az e-mail címed megerősítése után jár."), QStringLiteral("info"), {}, {});
    if (a.trial == QLatin1String("awaiting_card"))
        return mk(tr("A próbaegyenleget a kártya-ellenőrzés után írjuk jóvá."), QStringLiteral("info"),
                  QStringLiteral("web"), tr("Megnyitás a weben"));
    if (a.trial == QLatin1String("denied_card_used"))
        return mk(tr("Ezzel a kártyával már aktiváltak próbaegyenleget."), QStringLiteral("info"), {}, {});
    return {};
}

QVariantList SettingsCloudModel::notices() const
{
    QVariantList out;
    AppController* c = m_vm->controller();
    if (m_demo || !c || !c->cloud() || !c->cloud()->isLoggedIn()) return out;
    for (const Notice& n : c->cloud()->account().notices)
        out << QVariantMap{{QStringLiteral("level"), n.level}, {QStringLiteral("text"), n.message},
                           {QStringLiteral("url"), n.url}};
    return out;
}

bool SettingsCloudModel::termsPending() const
{
    AppController* c = m_vm->controller();
    if (m_demo || !c || !c->cloud() || !c->cloud()->isLoggedIn()) return false;
    const TermsStatus t = c->cloud()->account().terms;
    return t.needsAcceptance() || t.needsEarlyAcceptance();
}

QString SettingsCloudModel::termsText() const
{
    AppController* c = m_vm->controller();
    if (m_demo || !c || !c->cloud() || !c->cloud()->isLoggedIn()) return {};
    const AccountInfo a = c->cloud()->account();
    if (!a.valid) return {};
    const TermsStatus t = a.terms;
    if (t.needsAcceptance())
        return tr("Az ÁSZF új verzióját (%1) még nem fogadtad el — nélküle a feldolgozás nem indul.")
            .arg(t.currentVersion);
    if (t.needsEarlyAcceptance())
        return tr("Új ÁSZF lép életbe (%1). Már most elfogadhatod.").arg(t.upcomingVersion);
    if (!t.acceptedVersion.isEmpty())
        return tr("Elfogadott ÁSZF: %1").arg(t.acceptedVersion);
    return {};
}

// ---- minőség ---------------------------------------------------------------------------

QString SettingsCloudModel::sttTier() const
{
    return m_vm->draft().cloudSttTier == QLatin1String("fast") ? QStringLiteral("fast")
                                                               : QStringLiteral("accurate");
}

QString SettingsCloudModel::llmTier() const
{
    return m_vm->draft().cloudLlmTier == QLatin1String("fast") ? QStringLiteral("fast")
                                                               : QStringLiteral("accurate");
}

void SettingsCloudModel::setSttTier(const QString& tier)
{
    AppSettings& d = m_vm->draft();
    if (d.cloudSttTier == tier && d.cloudSttModel.isEmpty()) return;
    d.cloudSttTier = tier;
    d.cloudSttModel.clear();     // a szint választása kilép az Expert-módból (mint a régi ablakban)
    emit tiersChanged();
    m_vm->touch();
}

void SettingsCloudModel::setLlmTier(const QString& tier)
{
    AppSettings& d = m_vm->draft();
    if (d.cloudLlmTier == tier && d.cloudLlmModel.isEmpty()) return;
    d.cloudLlmTier = tier;
    d.cloudLlmModel.clear();
    emit tiersChanged();
    m_vm->touch();
}

QString SettingsCloudModel::expertName(bool stt) const
{
    const AppSettings& d = m_vm->draft();
    const QString id = stt ? d.cloudSttModel : d.cloudLlmModel;
    if (id.isEmpty()) return {};
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return id;
    const QVector<CloudModel> cat = c->cloud()->models();
    if (cat.isEmpty()) return id;
    const std::optional<CloudModel> m = findModel(cat, id);
    if (!m || m->isVirtual) return {};   // kivezetett modell → a szint érvényes
    return m->displayName.isEmpty() ? id : m->displayName;
}

QString SettingsCloudModel::tierInfo(bool stt) const
{
    const AppSettings& d = m_vm->draft();
    const QString kind = stt ? QStringLiteral("stt") : QStringLiteral("llm");
    const QString tier = stt ? sttTier() : llmTier();
    QStringList lines;
    if (stt)
        lines << (tier == QLatin1String("fast")
                      ? tr("Gyors: hamarabb elkészül és olcsóbb.")
                      : tr("Pontos: nevekhez és szakszavakhoz megbízhatóbb."));
    else
        lines << (tier == QLatin1String("fast")
                      ? tr("Gyors: rövidebb, olcsóbb összefoglaló-modell.")
                      : tr("Pontos: alaposabb összefoglaló-modell."));

    AppController* c = m_vm->controller();
    if (m_demo) {
        if (stt)
            lines << tr("%1 / óra.").arg(formatMoney(
                Money{tier == QLatin1String("fast") ? 1'500'000 : 6'000'000, QStringLiteral("USD")},
                MoneyStyle::Charge, lang()));
        return lines.join(QLatin1Char(' '));
    }
    if (!c || !c->cloud()) return lines.join(QLatin1Char(' '));
    const QVector<CloudModel> cat = c->cloud()->models();
    if (cat.isEmpty()) {
        lines << tr("A modell-lista még nem töltődött le — az árak a bejelentkezés után látszanak.");
        return lines.join(QLatin1Char(' '));
    }
    const QString expert = stt ? d.cloudSttModel : d.cloudLlmModel;
    std::optional<CloudModel> model = expert.isEmpty() ? std::nullopt : findModel(cat, expert);
    if (!expert.isEmpty() && !model)
        lines << tr("A korábban választott modell már nem érhető el; a szint modelljét használjuk.");
    if (!model) model = findTierModel(cat, kind, tier);
    if (!model) return lines.join(QLatin1Char(' '));
    // Tájékoztató ár a katalógusból (a kliens nem számol — a becslés a gateway dolga).
    if (stt && model->perHour.isValid())
        lines << tr("%1 / óra.").arg(formatMoney(model->perHour, MoneyStyle::Charge, lang()));
    if (stt && !model->diarization)
        lines << tr("Nem különíti el a beszélőket: az átiratban mindenki egy beszélőként jelenik meg.");
    const QString hint = d.languageHints.value(0);
    if (model->notRecommendedFor(hint))
        lines << (hint == QLatin1String("hu") ? tr("Magyar nyelvhez nem ajánljuk.")
                                              : tr("Ehhez a nyelvhez nem ajánljuk."));
    return lines.join(QLatin1Char(' '));
}

bool SettingsCloudModel::estimateBeforeRun() const
{
    return m_vm->draft().cloudEstimateBeforeRun;
}

void SettingsCloudModel::setEstimateBeforeRun(bool on)
{
    if (m_vm->draft().cloudEstimateBeforeRun == on) return;
    m_vm->draft().cloudEstimateBeforeRun = on;
    emit tiersChanged();
    m_vm->touch();
}

QString SettingsCloudModel::meetingLanguage() const
{
    return m_vm->draft().languageHints.value(0);
}

void SettingsCloudModel::setMeetingLanguage(const QString& code)
{
    const QStringList hints = code.isEmpty() ? QStringList() : QStringList{code};
    if (m_vm->draft().languageHints == hints) return;
    m_vm->draft().languageHints = hints;
    emit tiersChanged();
    m_vm->touch();
}

QVariantList SettingsCloudModel::meetingLanguages() const
{
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    QVariantList out{mk(QString(), tr("Automatikus"))};
    const QList<QPair<QString, QString>> langs = {
        {QStringLiteral("hu"), tr("Magyar")}, {QStringLiteral("en"), tr("Angol")},
        {QStringLiteral("de"), tr("Német")},  {QStringLiteral("fr"), tr("Francia")},
        {QStringLiteral("es"), tr("Spanyol")}, {QStringLiteral("it"), tr("Olasz")},
        {QStringLiteral("pl"), tr("Lengyel")}, {QStringLiteral("ro"), tr("Román")},
        {QStringLiteral("sk"), tr("Szlovák")},
    };
    bool found = meetingLanguage().isEmpty();
    for (const auto& l : langs) {
        out << mk(l.first, QStringLiteral("%1 (%2)").arg(l.second, l.first));
        found = found || l.first == meetingLanguage();
    }
    if (!found) out << mk(meetingLanguage(), meetingLanguage());   // kézzel beállított kód megőrzése
    return out;
}

// ---- műveletek -------------------------------------------------------------------------

void SettingsCloudModel::login()
{
    if (m_demo) { m_demoLoggedIn = true; emit accountChanged(); return; }
    if (SettingsDialogs* d = m_vm->dialogs()) {
        if (d->cloudLogin()) refresh();
        emit accountChanged();
        m_vm->touch();
    }
}

void SettingsCloudModel::logout()
{
    if (m_demo) { m_demoLoggedIn = false; emit accountChanged(); return; }
    AppController* c = m_vm->controller();
    if (c && c->cloud()) c->cloud()->logout();   // loggedOut → accountChanged
}

void SettingsCloudModel::topup()
{
    if (SettingsDialogs* d = m_vm->dialogs()) d->cloudTopup();
}

void SettingsCloudModel::refresh()
{
    AppController* c = m_vm->controller();
    if (!c || !c->cloud() || !c->cloud()->isLoggedIn()) return;
    c->cloud()->refreshAccount();
    c->cloud()->fetchModels();
}

void SettingsCloudModel::pickExpert(const QString& kind)
{
    // A Widgets-választó azonnal a beállításokba ír; a nézetmodell a settingsChanged jelre
    // átveszi (az alap és a piszkozat is frissül, a többi mentetlen változás megmarad).
    if (SettingsDialogs* d = m_vm->dialogs()) {
        d->cloudPickModel(kind);
        // A választó eredménye erősebb egy korábbi, még nem mentett szint-kattintásnál.
        if (AppController* c = m_vm->controller()) {
            const AppSettings s = c->settings()->settings();
            AppSettings& draft = m_vm->draft();
            draft.cloudSttModel = s.cloudSttModel;
            draft.cloudLlmModel = s.cloudLlmModel;
            draft.cloudSttTier = s.cloudSttTier;
            draft.cloudLlmTier = s.cloudLlmTier;
            m_vm->touch();
        }
        emit tiersChanged();
    }
}

void SettingsCloudModel::openTerms()
{
    if (SettingsDialogs* d = m_vm->dialogs()) {
        d->cloudTerms();
        emit accountChanged();
    }
}

void SettingsCloudModel::openDashboard()
{
    AppController* c = m_vm->controller();
    if (c && c->cloud() && m_vm->dialogs()) m_vm->dialogs()->openUrl(c->cloud()->account().dashboardUrl);
}

void SettingsCloudModel::openUsage()
{
    AppController* c = m_vm->controller();
    if (c && c->cloud() && m_vm->dialogs()) m_vm->dialogs()->openUrl(c->cloud()->account().usageUrl);
}

void SettingsCloudModel::openLink(const QString& url)
{
    if (!url.isEmpty() && m_vm->dialogs()) m_vm->dialogs()->openUrl(url);
}

void SettingsCloudModel::noticeAction()
{
    const QString action = notice().value(QStringLiteral("action")).toString();
    AppController* c = m_vm->controller();
    if (action == QLatin1String("login")) login();
    else if (action == QLatin1String("topup")) topup();
    else if (action == QLatin1String("update") && c && c->cloud()) openLink(c->cloud()->downloadUrl());
    else if (action == QLatin1String("web") && c && c->cloud()) {
        const QString support = !m_lastError.supportUrl.isEmpty() ? m_lastError.supportUrl
                                                                  : c->cloud()->account().dashboardUrl;
        openLink(support);
    }
}

// ---- várólista -------------------------------------------------------------------------

QString SettingsCloudModel::joinedEmail() const
{
    if (m_demo) return m_demoJoined;
    return m_vm->base().waitlistEmail;
}

QVariantList SettingsCloudModel::useCases() const
{
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    return {mk(QString(), tr("— (nem kötelező)")), mk(QStringLiteral("meetings"), tr("Megbeszélések")),
            mk(QStringLiteral("interviews"), tr("Interjúk")),
            mk(QStringLiteral("audio_files"), tr("Hangfájlok")), mk(QStringLiteral("other"), tr("Egyéb"))};
}

QString SettingsCloudModel::privacyUrl() const { return cloud::PrivacyUrl; }

void SettingsCloudModel::setWaitEmail(const QString& email)
{
    if (m_waitEmail == email) return;
    m_waitEmail = email;
    m_waitError.clear();
    emit waitlistChanged();
}

void SettingsCloudModel::setWaitUseCase(const QString& useCase)
{
    if (m_waitUseCase == useCase) return;
    m_waitUseCase = useCase;
    emit waitlistChanged();
}

void SettingsCloudModel::setWaitConsent(bool on)
{
    if (m_waitConsent == on) return;
    m_waitConsent = on;
    emit waitlistChanged();
}

void SettingsCloudModel::toggleWaitLanguage(const QString& code)
{
    if (!m_waitLanguages.removeOne(code)) m_waitLanguages << code;
    emit waitlistChanged();
}

bool SettingsCloudModel::waitCanSubmit() const
{
    WaitlistSignup s;
    s.email = m_waitEmail;
    s.consent = m_waitConsent;
    return !m_waitBusy && validateWaitlist(s).isEmpty();
}

void SettingsCloudModel::submitWaitlist()
{
    if (!waitCanSubmit()) return;
    if (m_demo) {
        m_demoJoined = m_waitEmail.trimmed();
        emit waitlistChanged();
        return;
    }
    AppController* c = m_vm->controller();
    if (!c || !c->cloud()) return;
    WaitlistSignup s;
    s.email = m_waitEmail.trimmed();
    s.consent = m_waitConsent;
    s.uiLanguage = lang();
    s.useCase = m_waitUseCase;
    s.meetingLanguages = m_waitLanguages;
    m_waitBusy = true;
    m_waitError.clear();
    emit waitlistChanged();
    c->cloud()->joinWaitlist(s);
}

void SettingsCloudModel::resetWaitlist()
{
    if (m_demo) { m_demoJoined.clear(); emit waitlistChanged(); return; }
    AppController* c = m_vm->controller();
    if (!c) return;
    AppSettings s = c->settings()->settings();
    s.waitlistEmail.clear();
    c->settings()->setSettings(s);
    emit waitlistChanged();
}

void SettingsCloudModel::loadDemo(const QString& state)
{
    m_demo = true;
    m_demoLoggedIn = state != QLatin1String("cloudOut");
    m_demoJoined.clear();
    m_waitEmail.clear();
    m_waitConsent = false;
    m_waitLanguages.clear();
    m_waitError.clear();
    emit accountChanged();
    emit tiersChanged();
    emit waitlistChanged();
}

} // namespace tanara_qml
