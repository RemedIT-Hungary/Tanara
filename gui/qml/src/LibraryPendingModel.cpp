#include "LibraryPendingModel.h"

#include "AppContext.h"
#include "LibraryDemoData.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"

namespace tanara_qml {

using tanara::PendingItem;
using tanara::PendingKind;

LibraryPendingModel::LibraryPendingModel(QObject* parent) : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &LibraryPendingModel::reload);

    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    connect(ctx, &AppContext::demoChanged, this, &LibraryPendingModel::reload);
    attach();
}

void LibraryPendingModel::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attach();
}

void LibraryPendingModel::attach()
{
    tanara::MeetingLibrary* lib = m_controller ? m_controller->library() : nullptr;
    if (lib != m_library) {
        if (m_library)
            m_library->disconnect(this);
        m_library = lib;
        if (lib) {
            const auto later = [this] { m_reloadTimer.start(); };
            connect(lib, &tanara::MeetingLibrary::pendingItemsChanged, this, later);
            connect(lib, &tanara::MeetingLibrary::reset, this, later);
        }
    }
    reload();
}

QVariantList LibraryPendingModel::build(const QVector<PendingItem>& pending)
{
    const int maxPerKind = 3;
    QVariantList out;

    // 1) Átírásra váró felvételek — egy sorban, a legfrissebbet nyitja meg.
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
    // 2) Elavult összefoglalók.
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
            {QStringLiteral("tab"), 2}});   // a Vezetői összefoglaló fül
    }
    // 3) Sikertelen átírások.
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
    QVector<PendingItem> pending;
    if (m_library)
        pending = m_library->pendingItems();
    else if (!m_controller && AppContext::instance()->demo())
        pending = demo::pendingItems();
    const QVariantList items = build(pending);
    if (items == m_items)
        return;
    m_items = items;
    emit itemsChanged();
}

} // namespace tanara_qml
