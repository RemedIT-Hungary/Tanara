#include "LibraryPendingModel.h"

#include "AppContext.h"
#include "ShellFormat.h"

#include "tanara/library/ILibraryFacade.h"

namespace tanara_qml {

using tanara::library::PendingItem;
using tanara::library::PendingKind;

LibraryPendingModel::LibraryPendingModel(QObject* parent) : QObject(parent)
{
    init();
    // Without an injected facade the model follows the App singleton's services.
    AppContext* ctx = AppContext::instance();
    const auto resolve = [this] {
        if (m_injected) return;
        m_facade = AppContext::instance()->services().library;
        attach();
    };
    connect(ctx, &AppContext::controllerChanged, this, resolve);
    connect(ctx, &AppContext::demoChanged, this, resolve);
    m_facade = ctx->services().library;
    attach();
}

LibraryPendingModel::LibraryPendingModel(tanara::library::ILibraryFacade* facade, QObject* parent)
    : QObject(parent), m_facade(facade), m_injected(true)
{
    init();
    attach();
}

void LibraryPendingModel::init()
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &LibraryPendingModel::reload);
}

void LibraryPendingModel::attach()
{
    tanara::library::ILibraryFacade* lib = m_facade;
    if (lib != m_attached) {
        if (m_attached)
            m_attached->disconnect(this);
        m_attached = lib;
        if (lib) {
            const auto later = [this] { m_reloadTimer.start(); };
            connect(lib, &tanara::library::ILibraryFacade::pendingItemsChanged, this, later);
            connect(lib, &tanara::library::ILibraryFacade::reset, this, later);
        }
    }
    reload();
}

QVariantList LibraryPendingModel::build(const QVector<PendingItem>& pending)
{
    const int maxPerKind = 3;
    QVariantList out;

    // 1) Recordings awaiting transcription: in one row, it opens the most recent.
    QVector<PendingItem> awaiting, stale, failed;
    for (const PendingItem& p : pending) {
        switch (p.kind) {
        case PendingKind::AwaitingTranscription: awaiting.append(p); break;
        case PendingKind::StaleSummary: stale.append(p); break;
        case PendingKind::TranscriptionFailed: failed.append(p); break;
        }
    }
    if (!awaiting.isEmpty()) {
        const PendingItem& first = awaiting.first();
        QString sub = first.title + QStringLiteral(" · ") + fmt::shortDate(first.startedAt);
        if (awaiting.size() > 1)
            sub = tr("Legutóbbi: %1").arg(sub);
        out.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("awaiting")},
            {QStringLiteral("meetingId"), first.meetingId},
            {QStringLiteral("title"), tr("%n megbeszélés átírásra vár", nullptr, int(awaiting.size()))},
            {QStringLiteral("subtitle"), sub},
            {QStringLiteral("actionLabel"), tr("Megnyitás")},
            {QStringLiteral("iconName"), QStringLiteral("file-text")},
            {QStringLiteral("tone"), QStringLiteral("accent")},
            {QStringLiteral("tab"), 0}});
    }
    // 2) Stale summaries.
    for (int i = 0; i < stale.size() && i < maxPerKind; ++i) {
        const PendingItem& p = stale.at(i);
        out.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("stale")},
            {QStringLiteral("meetingId"), p.meetingId},
            {QStringLiteral("title"), tr("Elavult összefoglaló")},
            {QStringLiteral("subtitle"), p.title + QStringLiteral(" · ") + tr("a beszélők változtak")},
            {QStringLiteral("actionLabel"), tr("Frissítés")},
            {QStringLiteral("iconName"), QStringLiteral("sparkles")},
            {QStringLiteral("tone"), QStringLiteral("warn")},
            {QStringLiteral("tab"), 2}});   // the Executive summary tab
    }
    // 3) Failed transcriptions.
    for (int i = 0; i < failed.size() && i < maxPerKind; ++i) {
        const PendingItem& p = failed.at(i);
        QString reason = p.error.message.trimmed();
        if (reason.size() > 70)
            reason = reason.left(69).trimmed() + QStringLiteral("…");
        out.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("failed")},
            {QStringLiteral("meetingId"), p.meetingId},
            {QStringLiteral("title"), tr("Sikertelen átírás")},
            {QStringLiteral("subtitle"),
             reason.isEmpty() ? p.title : p.title + QStringLiteral(" · ") + reason},
            {QStringLiteral("actionLabel"), tr("Megnézem")},
            {QStringLiteral("iconName"), QStringLiteral("triangle-alert")},
            {QStringLiteral("tone"), QStringLiteral("danger")},
            {QStringLiteral("tab"), 0}});
    }
    return out;
}

void LibraryPendingModel::reload()
{
    const QVector<PendingItem> pending = m_facade ? m_facade->pendingItems() : QVector<PendingItem>{};
    const QVariantList items = build(pending);
    if (items == m_items)
        return;
    m_items = items;
    emit itemsChanged();
}

} // namespace tanara_qml
