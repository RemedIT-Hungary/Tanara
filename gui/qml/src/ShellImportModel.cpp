#include "ShellImportModel.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "ShellFormat.h"

#include "tanara/library/IImportFacade.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace tanara_qml {

using tanara::library::ImportFileInfo;
using tanara::library::ImportSource;

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
    init();
    // Without an injected facade the model follows the App singleton's services.
    AppContext* ctx = AppContext::instance();
    const auto resolve = [this] {
        if (m_injected) return;
        m_facade = AppContext::instance()->services().import;
        attach();
    };
    connect(ctx, &AppContext::controllerChanged, this, resolve);
    connect(ctx, &AppContext::demoChanged, this, resolve);
    m_facade = ctx->services().import;
    attach();
}

ShellImportModel::ShellImportModel(tanara::library::IImportFacade* facade, QObject* parent)
    : QObject(parent), m_facade(facade), m_injected(true)
{
    init();
    attach();
}

void ShellImportModel::init()
{
    // While typing the title we do not compute on every key.
    m_suggestTimer.setSingleShot(true);
    m_suggestTimer.setInterval(300);
    connect(&m_suggestTimer, &QTimer::timeout, this, &ShellImportModel::refreshSuggestions);
    connect(this, &ShellImportModel::titleChanged, this, [this] {
        if (m_facade && m_facade->live()) m_suggestTimer.start();
    });
}

void ShellImportModel::attach()
{
    using tanara::library::IImportFacade;
    if (m_facade == m_attached)
        return;
    if (m_attached)
        m_attached->disconnect(this);
    m_attached = m_facade;
    IImportFacade* f = m_facade;
    if (!f)
        return;
    connect(f, &IImportFacade::probed, this, &ShellImportModel::onProbed);
    connect(f, &IImportFacade::progress, this,
            [this](const QString& id, int percent, int fileIndex, int fileCount) {
        if (id != m_importId) return;
        m_percent = percent;
        m_fileIndex = fileIndex;
        m_fileTotal = fileCount;
        emit runChanged();
    });
    connect(f, &IImportFacade::finished, this, [this](const tanara::Meeting& m) {
        if (m.id != m_importId) return;
        applyTags(m.id, m_runningTagIds);
        m_runningTagIds.clear();
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
        reset();   // the next opening starts with an empty form
        emit imported(m.id);
    });
    connect(f, &IImportFacade::failed, this,
            [this](const QString& id, const QString& message, const QString& detail) {
        if (id != m_importId) return;
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
        setError(message, detail);
        emit failed(message);
    });
    connect(f, &IImportFacade::cancelled, this, [this](const QString& id) {
        if (id != m_importId) return;
        // Cancelled: the form stays, the user can edit and restart.
        m_running = m_cancelling = false;
        m_importId.clear();
        emit runChanged();
    });
}

// ---- the files --------------------------------------------------------------------------

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
        // The file name is already in the row: it is dropped from the end of the error message ("...: file name").
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
        if (!r.probed || !r.info.ok) continue;   // a file that cannot be imported is left out
        out.append({r.path, r.split});
        if (infos) infos->append(r.info);
    }
    return out;
}

QVector<tanara::library::ImportPlannedTrack> ShellImportModel::plan() const
{
    QVector<ImportFileInfo> infos;
    const QVector<ImportSource> src = sources(&infos);
    return tanara::library::audioimport::planTracks(src, infos);
}

QStringList ShellImportModel::trackNames() const
{
    QStringList out;
    for (const tanara::library::ImportPlannedTrack& t : plan()) out << t.name;
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
        && m_facade && m_facade->canStart(m_demoState);
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
        if (m_facade)
            m_facade->probeAsync(path);
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
        r.split = tanara::library::audioimport::splitByDefault(info);
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
    m_ownTrack = -1;   // the tracks' numbering changed
    emit filesChanged();
}

// The default title and date from the files, until the user has edited them.
void ShellImportModel::refreshDefaults()
{
    if (!m_titleEdited) {
        QStringList paths;
        for (const Row& r : m_rows)
            if (!r.probed || r.info.ok) paths << r.path;
        const QString t = paths.isEmpty() ? QString() : tanara::library::audioimport::defaultTitle(paths);
        if (t != m_title) { m_title = t; emit titleChanged(); }
    }
    if (!m_dateEdited) {
        QVector<ImportFileInfo> infos;
        sources(&infos);
        const QString d = infos.isEmpty() ? QString()
                        : tanara::library::audioimport::defaultStart(infos).toString(kDateFormat);
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
    if (!m_titleEdited) refreshDefaults();   // when cleared it reverts to the value computed from the file name
}

void ShellImportModel::setDateText(const QString& text)
{
    if (text == m_dateText) return;
    m_dateText = text;
    m_dateEdited = true;
    emit dateChanged();
    emit filesChanged();   // canStart depends on the date too
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
    const QDateTime best = tanara::library::audioimport::defaultStart(infos);
    for (const ImportFileInfo& i : infos)
        if (i.createdAt.isValid() && i.createdAt == best)
            return tr("A fájlba írt készítési időből.");
    return tr("A fájl módosítási idejéből — ha máskor készült, írd át.");
}

// ---- run -----------------------------------------------------------------------------

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
    if (!m_facade || !m_facade->live() || !canStart()) return false;
    tanara::library::ImportRequest req;
    req.sources = sources(nullptr);
    req.title = m_title.trimmed();
    req.startedAt = parseDate(m_dateText);
    req.ownTrack = m_ownTrack;
    clearError();
    const QString id = m_facade->start(req);
    if (id.isEmpty()) return false;
    m_importId = id;
    m_running = true;
    m_cancelling = false;
    m_percent = -1;
    m_fileIndex = 0;
    m_fileTotal = int(req.sources.size());
    m_runningTitle = req.title;
    m_runningTagIds = tagIds();
    emit runChanged();
    emit filesChanged();
    return true;
}

void ShellImportModel::cancel()
{
    if (!m_running || m_cancelling) return;
    m_cancelling = true;
    emit runChanged();
    if (m_facade)
        m_facade->cancel(m_importId);
}

void ShellImportModel::reset()
{
    m_suggestTimer.stop();
    m_tags.clear();
    m_suggestions.clear();
    m_dismissed.clear();
    emit tagsChanged();
    emit suggestionsChanged();
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

// ---- tags ----------------------------------------------------------------------

QVariantList ShellImportModel::tags() const
{
    QVariantList out;
    for (const TagRef& t : m_tags)
        out << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("name"), t.name}};
    return out;
}

QStringList ShellImportModel::tagIds() const
{
    QStringList out;
    for (const TagRef& t : m_tags) out << t.id;
    return out;
}

QVariantList ShellImportModel::suggestions() const
{
    QVariantList out;
    for (const Suggestion& s : m_suggestions)
        out << QVariantMap{{QStringLiteral("id"), s.id}, {QStringLiteral("name"), s.name},
                           {QStringLiteral("isNew"), s.isNew}, {QStringLiteral("source"), s.source},
                           {QStringLiteral("reason"), s.reason}};
    return out;
}

QString ShellImportModel::suggestionReason() const
{
    return m_suggestions.isEmpty() ? QString() : m_suggestions.first().reason;
}

bool ShellImportModel::addTag(const QString& name)
{
    const QString n = name.simplified();
    if (n.isEmpty()) return false;
    if (!m_facade) return false;
    const tanara::Tag t = m_facade->createTag(n);     // for an existing key, the existing tag
    if (!t.isValid()) return false;
    const TagRef ref{t.id, t.name};
    for (const TagRef& t : std::as_const(m_tags))
        if (t.id == ref.id) return false;
    m_tags.append(ref);
    emit tagsChanged();
    // An attached tag drops out of the suggestions.
    for (int i = 0; i < m_suggestions.size(); ++i)
        if (m_suggestions.at(i).id == ref.id
            || m_suggestions.at(i).name.compare(ref.name, Qt::CaseInsensitive) == 0) {
            m_suggestions.removeAt(i);
            emit suggestionsChanged();
            break;
        }
    return true;
}

void ShellImportModel::removeTag(const QString& id)
{
    for (int i = 0; i < m_tags.size(); ++i) {
        if (m_tags.at(i).id != id) continue;
        m_tags.removeAt(i);
        emit tagsChanged();
        return;
    }
}

void ShellImportModel::acceptSuggestion(int index)
{
    if (index < 0 || index >= m_suggestions.size()) return;
    addTag(m_suggestions.at(index).name);
}

void ShellImportModel::dismissSuggestion(int index)
{
    if (index < 0 || index >= m_suggestions.size()) return;
    m_dismissed << m_suggestions.at(index).name.toLower();
    m_suggestions.removeAt(index);
    emit suggestionsChanged();
}

void ShellImportModel::refreshSuggestions()
{
    m_suggestTimer.stop();
    if (!m_facade || !m_facade->live()) return;   // demo: the fictional suggestion stays
    QVector<Suggestion> out;
    const QString title = m_title.trimmed();
    if (!title.isEmpty()) {
        const QStringList applied = tagIds();
        for (const tanara::TagSuggestion& t : m_facade->suggestionsFor(title)) {
            if ((!t.tagId.isEmpty() && applied.contains(t.tagId)) || m_dismissed.contains(t.name.toLower()))
                continue;
            bool onIt = false;
            for (const TagRef& r : std::as_const(m_tags))
                if (r.name.compare(t.name, Qt::CaseInsensitive) == 0) onIt = true;
            if (onIt) continue;
            Suggestion s;
            s.id = t.tagId;
            s.name = t.name;
            s.isNew = t.isNew;
            s.source = t.source == tanara::SuggestionSource::Llm ? QStringLiteral("llm")
                     : t.source == tanara::SuggestionSource::Cooccur ? QStringLiteral("cooccur")
                                                                      : QStringLiteral("similar");
            // A short reason: the similar title, otherwise the shared participant / term.
            for (const tanara::SuggestionReason& r : t.reasons) {
                if (r.values.isEmpty()) continue;
                if (r.kind == tanara::ReasonKind::Title) {
                    s.reason = tr("hasonló cím: „%1”").arg(r.values.first());
                    break;
                }
                if (s.reason.isEmpty())
                    s.reason = r.kind == tanara::ReasonKind::Participant
                        ? tr("közös résztvevő: %1").arg(r.values.mid(0, 2).join(QStringLiteral(", ")))
                        : tr("közös kifejezések: %1").arg(r.values.mid(0, 3).join(QStringLiteral(", ")));
            }
            out.append(s);
        }
    }
    bool same = out.size() == m_suggestions.size();
    for (int i = 0; same && i < out.size(); ++i)
        same = out.at(i).id == m_suggestions.at(i).id && out.at(i).name == m_suggestions.at(i).name
            && out.at(i).reason == m_suggestions.at(i).reason;
    if (same) return;
    m_suggestions = out;
    emit suggestionsChanged();
}

void ShellImportModel::applyTags(const QString& meetingId, const QStringList& ids)
{
    if (m_facade && !ids.isEmpty())
        m_facade->applyTags(meetingId, ids);
}

// ---- demo (screenshot) ------------------------------------------------------------------

void ShellImportModel::setDemoState(const QString& state)
{
    if (state == m_demoState) return;
    m_demoState = state;
    emit demoStateChanged();
    if (!m_facade) return;
    // Only a fictional facade has a design state to serve.
    if (const auto scenario = m_facade->scenario(m_demoState))
        applyScenario(*scenario);
}

void ShellImportModel::applyScenario(const tanara::library::ImportScenario& sc)
{
    m_rows.clear();
    for (const tanara::library::ImportScenarioFile& f : sc.files) {
        Row r;
        r.path = f.path;
        r.probed = f.probed;
        r.info = f.info;
        r.split = f.split;
        m_rows.append(r);
    }
    m_ownTrack = sc.ownTrack;
    m_error.clear();
    m_errorDetail.clear();
    m_running = m_cancelling = false;
    m_titleEdited = m_dateEdited = false;
    refreshDefaults();
    m_tags.clear();
    m_suggestions.clear();
    m_dismissed.clear();
    for (const auto& t : sc.tags)
        m_tags.append({t.id, t.name});
    if (sc.suggestion) {
        Suggestion sug;
        sug.name = sc.suggestion->name;
        sug.id = sc.suggestion->id;
        sug.source = sc.suggestion->source;
        sug.reason = sc.suggestion->reason;
        m_suggestions.append(sug);
    }
    emit tagsChanged();
    emit suggestionsChanged();
    if (!sc.error.isEmpty())
        m_error = sc.error;
    if (sc.running) {
        m_running = true;
        m_percent = sc.percent;
        m_fileIndex = sc.fileIndex;
        m_fileTotal = sc.fileTotal;
        m_runningTitle = m_title;
    }
    emit titleChanged();
    emit dateChanged();
    emit filesChanged();
    emit runChanged();
    emit errorChanged();
}

} // namespace tanara_qml
