#include "ShellImportModel.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace tanara_qml {

using tanara::ImportFileInfo;
using tanara::ImportSource;

namespace {

const QString kDateFormat = QStringLiteral("yyyy-MM-dd HH:mm");

QString channelsText(int channels)
{
    if (channels == 1) return ShellImportModel::tr("mono");
    if (channels == 2) return ShellImportModel::tr("sztereó");
    return ShellImportModel::tr("%n csatorna", nullptr, channels);
}

} // namespace

ShellImportModel::ShellImportModel(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    attach();
}

void ShellImportModel::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attach();
}

void ShellImportModel::attach()
{
    if (m_controller == m_attached)
        return;
    if (m_attached) {
        m_attached->disconnect(this);
        if (m_attached->importer()) m_attached->importer()->disconnect(this);
    }
    m_attached = m_controller;
    tanara::AppController* c = m_controller;
    if (!c)
        return;
    tanara::AudioImporter* imp = c->importer();
    connect(imp, &tanara::AudioImporter::probed, this, &ShellImportModel::onProbed);
    connect(imp, &tanara::AudioImporter::progress, this,
            [this](const QString& id, int percent, int fileIndex, int fileCount) {
        if (id != m_importId) return;
        m_percent = percent;
        m_fileIndex = fileIndex;
        m_fileTotal = fileCount;
        emit runChanged();
    });
    connect(c, &tanara::AppController::importFinished, this, [this](const tanara::Meeting& m) {
        if (m.id != m_importId) return;
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
        reset();   // a következő megnyitás üres űrlappal indul
        emit imported(m.id);
    });
    connect(c, &tanara::AppController::importFailed, this,
            [this](const QString& id, const QString& message, const QString& detail) {
        if (id != m_importId) return;
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
        setError(message, detail);
        emit failed(message);
    });
    connect(c, &tanara::AppController::importCancelled, this, [this](const QString& id) {
        if (id != m_importId) return;
        // Megszakítva: az űrlap megmarad, a felhasználó módosíthat és újraindíthatja.
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
    });
}

// ---- a fájlok --------------------------------------------------------------------------

QVariantList ShellImportModel::files() const
{
    QVariantList out;
    for (const Row& r : m_rows) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), r.path);
        m.insert(QStringLiteral("name"), QFileInfo(r.path).fileName());
        const bool ok = r.probed && r.info.ok;
        m.insert(QStringLiteral("state"), !r.probed ? QStringLiteral("probing")
                                         : ok ? QStringLiteral("ok") : QStringLiteral("error"));
        // A sorban a fájlnév már ott van: a hibaüzenet végéről ("…: fájlnév") lemarad.
        QString problem = r.probed && !r.info.ok ? r.info.error : QString();
        const QString tail = QStringLiteral(": ") + QFileInfo(r.path).fileName();
        if (problem.endsWith(tail)) problem.chop(tail.size());
        m.insert(QStringLiteral("error"), problem);
        m.insert(QStringLiteral("video"), ok && r.info.hasVideo);
        m.insert(QStringLiteral("channels"), ok ? r.info.channels : 0);
        m.insert(QStringLiteral("canSplit"), ok && r.info.channels >= 2);
        m.insert(QStringLiteral("split"), ok && r.split && r.info.channels >= 2);
        QString meta, hint;
        if (ok) {
            QStringList parts;
            parts << (r.info.durationMs > 0 ? jobsupport::formatDuration(r.info.durationMs)
                                            : tr("ismeretlen hossz"));
            parts << channelsText(r.info.channels);
            if (r.info.hasVideo) parts << tr("videó hangja");
            parts << fmt::uiLocale().formattedDataSize(r.info.sizeBytes, 1, QLocale::DataSizeTraditionalFormat);
            meta = parts.join(QStringLiteral(" · "));
            if (r.info.channels == 2)
                hint = r.split ? tr("A bal és a jobb csatorna két külön sáv lesz.")
                               : tr("Akkor kapcsold be, ha a bal és a jobb csatornán más-más ember "
                                    "mikrofonja szól. Kikapcsolva a fájl egyetlen sztereó sáv marad.");
            else if (r.info.channels > 2)
                hint = r.split ? tr("Mind a(z) %1 csatorna külön sáv lesz.").arg(r.info.channels)
                               : tr("Kikapcsolva a(z) %1 csatorna egyetlen sztereó sávba keveredik.")
                                     .arg(r.info.channels);
        }
        m.insert(QStringLiteral("meta"), meta);
        m.insert(QStringLiteral("splitHint"), hint);
        out.append(m);
    }
    return out;
}

bool ShellImportModel::probing() const
{
    for (const Row& r : m_rows)
        if (!r.probed) return true;
    return false;
}

QVector<ImportSource> ShellImportModel::sources(QVector<ImportFileInfo>* infos) const
{
    QVector<ImportSource> out;
    for (const Row& r : m_rows) {
        if (!r.probed || !r.info.ok) continue;   // a nem importálható fájl kimarad
        out.append({r.path, r.split});
        if (infos) infos->append(r.info);
    }
    return out;
}

QVector<tanara::ImportPlannedTrack> ShellImportModel::plan() const
{
    QVector<ImportFileInfo> infos;
    const QVector<ImportSource> src = sources(&infos);
    return tanara::audioimport::planTracks(src, infos);
}

QStringList ShellImportModel::trackNames() const
{
    QStringList out;
    for (const tanara::ImportPlannedTrack& t : plan()) out << t.name;
    return out;
}

int ShellImportModel::trackCount() const { return int(plan().size()); }

QString ShellImportModel::trackSummary() const
{
    if (m_rows.isEmpty()) return QString();
    if (probing()) return tr("A fájlok adatainak beolvasása…");
    const int n = trackCount();
    if (n == 0) return tr("Egyik fájl sem importálható.");
    int skipped = 0;
    for (const Row& r : m_rows)
        if (!r.info.ok) ++skipped;
    QString text = tr("%n sáv lesz belőle.", nullptr, n);
    if (skipped > 0)
        text += QLatin1Char(' ') + tr("%n fájl kimarad.", nullptr, skipped);
    return text;
}

void ShellImportModel::setOwnTrack(int index)
{
    if (index < -1 || index >= trackCount()) index = -1;
    if (index == m_ownTrack) return;
    m_ownTrack = index;
    emit filesChanged();
}

bool ShellImportModel::canStart() const
{
    return !m_running && !m_rows.isEmpty() && !probing() && trackCount() > 0 && dateValid()
        && (m_controller || !m_demoState.isEmpty());
}

int ShellImportModel::addFiles(const QVariantList& pathsOrUrls)
{
    int added = 0;
    for (const QVariant& v : pathsOrUrls) {
        QString path = v.toString();
        const QUrl url = v.metaType().id() == QMetaType::QUrl ? v.toUrl() : QUrl(path);
        if (url.isLocalFile()) path = url.toLocalFile();
        if (path.isEmpty()) continue;
        const QFileInfo fi(path);
        if (fi.isDir()) continue;
        path = fi.absoluteFilePath();
        bool known = false;
        for (const Row& r : m_rows)
            if (r.path == path) { known = true; break; }
        if (known) continue;
        Row row;
        row.path = path;
        m_rows.append(row);
        ++added;
        if (m_controller)
            m_controller->importer()->probeAsync(path);
    }
    if (added > 0) {
        clearError();
        refreshDefaults();
        emit filesChanged();
    }
    return added;
}

void ShellImportModel::onProbed(const ImportFileInfo& info)
{
    bool any = false;
    for (Row& r : m_rows) {
        if (r.probed || r.path != info.path) continue;
        r.info = info;
        r.probed = true;
        r.split = tanara::audioimport::splitByDefault(info);
        any = true;
    }
    if (!any) return;
    m_ownTrack = -1;
    refreshDefaults();
    emit filesChanged();
}

void ShellImportModel::removeFile(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    m_rows.removeAt(row);
    m_ownTrack = -1;
    refreshDefaults();
    emit filesChanged();
}

void ShellImportModel::setSplit(int row, bool split)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).split == split) return;
    m_rows[row].split = split;
    m_ownTrack = -1;   // a sávok sorszáma megváltozott
    emit filesChanged();
}

// A cím és a dátum alapértelmezése a fájlokból — amíg a felhasználó nem írta át.
void ShellImportModel::refreshDefaults()
{
    if (!m_titleEdited) {
        QStringList paths;
        for (const Row& r : m_rows)
            if (!r.probed || r.info.ok) paths << r.path;
        const QString t = paths.isEmpty() ? QString() : tanara::audioimport::defaultTitle(paths);
        if (t != m_title) { m_title = t; emit titleChanged(); }
    }
    if (!m_dateEdited) {
        QVector<ImportFileInfo> infos;
        sources(&infos);
        const QString d = infos.isEmpty() ? QString()
                        : tanara::audioimport::defaultStart(infos).toString(kDateFormat);
        if (d != m_dateText) m_dateText = d;
        emit dateChanged();
    }
}

void ShellImportModel::setTitle(const QString& title)
{
    if (title == m_title) return;
    m_title = title;
    m_titleEdited = !title.trimmed().isEmpty();
    emit titleChanged();
    if (!m_titleEdited) refreshDefaults();   // kiürítve visszaáll a fájlnévből számoltra
}

void ShellImportModel::setDateText(const QString& text)
{
    if (text == m_dateText) return;
    m_dateText = text;
    m_dateEdited = true;
    emit dateChanged();
    emit filesChanged();   // a canStart a dátumtól is függ
}

QDateTime ShellImportModel::parseDate(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(
        "^\\s*(\\d{4})\\D{1,3}(\\d{1,2})\\D{1,3}(\\d{1,2})\\D{0,3}(?:(\\d{1,2})\\D(\\d{2}))?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return {};
    const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    const QTime t = m.captured(4).isEmpty() ? QTime(12, 0)
                                            : QTime(m.captured(4).toInt(), m.captured(5).toInt());
    if (!d.isValid() || !t.isValid() || d.year() < 1970) return {};
    return QDateTime(d, t);
}

QString ShellImportModel::dateHint() const
{
    if (!dateValid() && !m_dateText.isEmpty())
        return tr("Így írd: 2026-03-05 14:30");
    if (m_dateEdited) return QString();
    QVector<ImportFileInfo> infos;
    sources(&infos);
    if (infos.isEmpty()) return QString();
    const QDateTime best = tanara::audioimport::defaultStart(infos);
    for (const ImportFileInfo& i : infos)
        if (i.createdAt.isValid() && i.createdAt == best)
            return tr("A fájlba írt készítési időből.");
    return tr("A fájl módosítási idejéből — ha máskor készült, írd át.");
}

// ---- futás -----------------------------------------------------------------------------

QString ShellImportModel::progressText() const
{
    if (m_cancelling) return tr("Megszakítás…");
    if (m_percent < 0) return tr("Előkészítés…");
    if (m_fileTotal > 1)
        return tr("%1% · %2 / %3 fájl").arg(m_percent).arg(qMin(m_fileIndex + 1, m_fileTotal)).arg(m_fileTotal);
    return QStringLiteral("%1%").arg(m_percent);
}

bool ShellImportModel::start()
{
    if (!m_controller || !canStart()) return false;
    tanara::ImportRequest req;
    req.sources = sources(nullptr);
    req.title = m_title.trimmed();
    req.startedAt = parseDate(m_dateText);
    req.ownTrack = m_ownTrack;
    clearError();
    const QString id = m_controller->importAudio(req);
    if (id.isEmpty()) return false;
    m_importId = id;
    m_running = true;
    m_cancelling = false;
    m_percent = -1;
    m_fileIndex = 0;
    m_fileTotal = int(req.sources.size());
    m_runningTitle = req.title;
    emit runChanged();
    emit filesChanged();
    return true;
}

void ShellImportModel::cancel()
{
    if (!m_running || m_cancelling) return;
    m_cancelling = true;
    emit runChanged();
    if (m_controller)
        m_controller->cancelJob(m_importId, tanara::JobKind::Import);
}

void ShellImportModel::reset()
{
    m_rows.clear();
    m_ownTrack = -1;
    m_title.clear();
    m_dateText.clear();
    m_titleEdited = m_dateEdited = false;
    emit titleChanged();
    emit dateChanged();
    emit filesChanged();
    clearError();
}

void ShellImportModel::setError(const QString& message, const QString& detail)
{
    if (message == m_error && detail == m_errorDetail) return;
    m_error = message;
    m_errorDetail = detail;
    emit errorChanged();
}

void ShellImportModel::clearError() { setError(QString()); }

// ---- demó (képernyőkép) ------------------------------------------------------------------

void ShellImportModel::setDemoState(const QString& state)
{
    if (state == m_demoState) return;
    m_demoState = state;
    emit demoStateChanged();
    if (!m_controller) loadDemo();
}

void ShellImportModel::loadDemo()
{
    const auto file = [](const QString& path, qint64 ms, int channels, qint64 bytes, bool video = false) {
        Row r;
        r.path = path;
        r.probed = true;
        r.info.path = path;
        r.info.ok = true;
        r.info.durationMs = ms;
        r.info.channels = channels;
        r.info.sizeBytes = bytes;
        r.info.hasVideo = video;
        r.info.modifiedAt = QDateTime(QDate(2026, 9, 24), QTime(10, 5));
        return r;
    };
    m_rows.clear();
    m_ownTrack = -1;
    m_error.clear();
    m_errorDetail.clear();
    m_running = m_cancelling = false;
    m_titleEdited = m_dateEdited = false;

    const QString dir = QStringLiteral("/home/minta/Felvételek/");
    if (m_demoState == QLatin1String("split")) {
        m_rows << file(dir + QStringLiteral("Interjú – Varga Nóra.wav"), 2538000, 2, 447000000);
        m_rows.last().split = true;
        m_ownTrack = 0;
    } else if (m_demoState == QLatin1String("probing")) {
        m_rows << file(dir + QStringLiteral("Fókuszcsoport 3 – terem.wav"), 4561000, 2, 803000000);
        Row r;
        r.path = dir + QStringLiteral("Fókuszcsoport 3 – moderátor.m4a");
        m_rows << r;
    } else if (m_demoState == QLatin1String("error")) {
        m_rows << file(dir + QStringLiteral("Fókuszcsoport 3 – terem.wav"), 4561000, 2, 803000000);
        Row r;
        r.path = dir + QStringLiteral("jegyzetek.pdf");
        r.probed = true;
        r.info.path = r.path;
        r.info.error = tr("Nem hang- vagy videófájl (nem olvasható be): %1").arg(QStringLiteral("jegyzetek.pdf"));
        m_rows << r;
    } else if (!m_demoState.isEmpty()) {   // "files", "progress"
        m_rows << file(dir + QStringLiteral("Fókuszcsoport 3 – terem.wav"), 4561000, 2, 803000000)
               << file(dir + QStringLiteral("Fókuszcsoport 3 – moderátor.m4a"), 4558000, 1, 36400000)
               << file(dir + QStringLiteral("Fókuszcsoport 3 – kamera.mp4"), 4570000, 2, 1932000000, true);
    }
    refreshDefaults();
    if (m_demoState == QLatin1String("failed"))
        m_error = tr("Nem sikerült beolvasni: %1").arg(QStringLiteral("Fókuszcsoport 3 – kamera.mp4"));
    if (m_demoState == QLatin1String("progress")) {
        m_running = true;
        m_percent = 42;
        m_fileIndex = 1;
        m_fileTotal = 3;
        m_runningTitle = m_title;
    }
    emit titleChanged();
    emit dateChanged();
    emit filesChanged();
    emit runChanged();
    emit errorChanged();
}

} // namespace tanara_qml
