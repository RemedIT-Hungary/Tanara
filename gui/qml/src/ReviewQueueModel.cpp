#include "ReviewQueueModel.h"

#include "tanara/edit/SpeakerAnalysis.h"

#include <algorithm>
#include <cmath>

using tanara::EditorUtterance;

namespace tanara_qml {

namespace {

bool flagged(const EditorUtterance& u)
{
    return u.confirmed || u.manuallyCorrected || u.ignored;
}

bool isDecidedState(ReviewQueueModel::State s)
{
    return s == ReviewQueueModel::Decided || s == ReviewQueueModel::Ignored;
}

} // namespace

ReviewQueueModel::ReviewQueueModel(TranscriptEditorViewModel* owner)
    : QAbstractListModel(owner), m_vm(owner)
{
}

QString ReviewQueueModel::stateName(State s)
{
    switch (s) {
    case Pending: return QStringLiteral("pending");
    case Decided: return QStringLiteral("decided");
    case Later: return QStringLiteral("later");
    case Ignored: return QStringLiteral("ignored");
    case AlreadyDecided: return QStringLiteral("alreadyDecided");
    }
    return {};
}

int ReviewQueueModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> ReviewQueueModel::roleNames() const
{
    return {
        {UtteranceIdRole, "utteranceId"},
        {OrderRole, "order"},
        {StateRole, "state"},
        {CurrentRole, "current"},
        {ReasonKindRole, "reasonKind"},
        {ReasonTitleRole, "reasonTitle"},
        {ReasonsRole, "reasons"},
        {GroupIdRole, "groupId"},
        {LikelyKeyRole, "likelyKey"},
        {LikelyNameRole, "likelyName"},
        {LikelyNewSideRole, "likelyNewSide"},
        {SuggestionUpdatedRole, "suggestionUpdated"},
        {SpeakerKeyRole, "speakerKey"},
        {SpeakerNameRole, "speakerName"},
        {ColorIndexRole, "colorIndex"},
        {LaneRole, "lane"},
        {OriginalKeyRole, "originalKey"},
        {OriginalNameRole, "originalName"},
        {StartMsRole, "startMs"},
        {EndMsRole, "endMs"},
        {TimeLabelRole, "timeLabel"},
        {LineTextRole, "lineText"},
        {DecidedKindRole, "decidedKind"},
        {SampleStateRole, "sampleState"},
        {AdmittedRole, "admitted"},
        {ConsequenceRole, "consequence"},
        {RoundRole, "round"},
    };
}

const EditorUtterance* ReviewQueueModel::utt(const QString& id) const
{
    const int i = m_vm->utteranceIndexOf(id);
    return i >= 0 && i < m_vm->utterances().size() ? &m_vm->utterances()[i] : nullptr;
}

QVariant ReviewQueueModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_items.size()) return {};
    const Item& it = m_items[row];
    switch (role) {
    case UtteranceIdRole: return it.id;
    case OrderRole: return row;
    case StateRole: return stateName(it.state);
    case CurrentRole: return row == m_current;
    case ReasonKindRole: return it.reason.kind;
    case ReasonTitleRole: return it.reason.title;
    case ReasonsRole: return it.reason.evidence;
    case GroupIdRole: return it.reason.groupId;
    case LikelyKeyRole: return it.likelyKey;
    case LikelyNameRole: {
        if (!it.likelyKey.isEmpty()) {
            const QString n = m_vm->speakerView(it.likelyKey).name;
            return n.isEmpty() ? it.likelyName : n;
        }
        return it.likelyName;
    }
    case LikelyNewSideRole: return it.likelyNewSide;
    case SuggestionUpdatedRole: return it.suggestionUpdated;
    case RoundRole: return it.round;
    case AdmittedRole: return it.admitted;
    case ConsequenceRole: return it.consequence;
    case OriginalKeyRole: return it.originalKey;
    case OriginalNameRole: return m_vm->speakerView(it.originalKey).name;
    default: break;
    }
    const int ui = m_vm->utteranceIndexOf(it.id);
    if (ui < 0) {
        switch (role) {
        case StartMsRole:
        case EndMsRole:
        case ColorIndexRole: return 0;
        case LaneRole: return -1;
        default: return QString();
        }
    }
    const EditorUtterance& u = m_vm->utterances()[ui];
    switch (role) {
    case SpeakerKeyRole: return u.speakerKey;
    case SpeakerNameRole: return m_vm->speakerView(u.speakerKey).name;
    case ColorIndexRole: return m_vm->speakerView(u.speakerKey).colorIndex;
    case LaneRole: return m_vm->speakerView(u.speakerKey).lane;
    case StartMsRole: return int(u.startMs);
    case EndMsRole: return int(u.endMs);
    case TimeLabelRole: return TranscriptEditorViewModel::timeLabel(u.startMs);
    case LineTextRole: return u.text;
    case DecidedKindRole:
        if (u.ignored) return QStringLiteral("ignored");
        if (u.manuallyCorrected) return QStringLiteral("corrected");
        if (u.confirmed) return QStringLiteral("confirmed");
        return QString();
    case SampleStateRole: return m_vm->sampleState(ui);
    default: return {};
    }
}

QVariantMap ReviewQueueModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    QVariantMap out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return out;
}

QString ReviewQueueModel::currentUtterance() const
{
    return m_current >= 0 && m_current < m_items.size() ? m_items[m_current].id : QString();
}

// ---- állapot ----------------------------------------------------------------

ReviewQueueModel::State ReviewQueueModel::stateOf(const Item& it) const
{
    const EditorUtterance* u = utt(it.id);
    if (!u) return AlreadyDecided;
    if (u->ignored) return Ignored;
    if (u->confirmed || u->manuallyCorrected) return Decided;
    if (it.later) return Later;
    if (!m_doubt.contains(it.id)) return AlreadyDecided;
    return Pending;
}

QString ReviewQueueModel::sigOf(const QString& id) const
{
    const EditorUtterance* u = utt(id);
    if (!u) return {};
    return QStringLiteral("%1|%2%3%4%5").arg(u->speakerKey).arg(int(u->confirmed)).arg(int(u->manuallyCorrected))
        .arg(int(u->ignored)).arg(int(u->noisy));
}

bool ReviewQueueModel::isOpen(const Item& it) const
{
    return it.state == Pending;
}

int ReviewQueueModel::decidedCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const Item& i) { return i.state == Decided; }));
}

int ReviewQueueModel::ignoredCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const Item& i) { return i.state == Ignored; }));
}

int ReviewQueueModel::laterCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const Item& i) { return i.state == Later; }));
}

int ReviewQueueModel::alreadyDecidedCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const Item& i) { return i.state == AlreadyDecided; }));
}

int ReviewQueueModel::openCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [this](const Item& i) { return isOpen(i); }));
}

// ---- kétes sorok ------------------------------------------------------------

QHash<QString, ReviewQueueModel::Doubt> ReviewQueueModel::computeDoubt(
    const QVector<TranscriptEditorViewModel::ReviewView>& views, const QVector<EditorUtterance>& utts) const
{
    QHash<QString, const EditorUtterance*> byId;
    byId.reserve(utts.size());
    for (const EditorUtterance& u : utts) byId.insert(u.id, &u);
    QHash<QString, Doubt> out;
    // 1) A döntést kérő csoportok sorai (egy sor az első csoportjában).
    for (const auto& v : views) {
        if (!v.actionable) continue;
        Doubt d{v.id, v.kind, v.title, v.evidence, v.proposedKey, v.proposedName, v.newSide};
        for (const QString& id : v.ids) {
            const EditorUtterance* u = byId.value(id);
            if (!u || flagged(*u) || out.contains(id)) continue;
            out.insert(id, d);
        }
    }
    // 2) Minden más hangra kétes sor (pl. egy kihagyott csoport, a rövid sorok közül).
    for (const EditorUtterance& u : utts) {
        if (!u.uncertain || flagged(u) || out.contains(u.id)) continue;
        Doubt d;
        d.kind = QStringLiteral("uncertain");
        d.proposedKey = u.likelySpeakerKey;
        out.insert(u.id, d);
    }
    return out;
}

void ReviewQueueModel::sortByTime(QStringList& ids) const
{
    std::stable_sort(ids.begin(), ids.end(), [this](const QString& a, const QString& b) {
        return m_vm->utteranceIndexOf(a) < m_vm->utteranceIndexOf(b);
    });
}

ReviewQueueModel::Item ReviewQueueModel::makeItem(const QString& id, const Doubt& d, bool admitted) const
{
    Item it;
    it.id = id;
    const EditorUtterance* u = utt(id);
    it.originalKey = u ? u->speakerKey : QString();
    it.reason = d;
    it.likelyKey = d.proposedKey;
    it.likelyName = d.proposedName;
    it.likelyNewSide = d.newSide;
    it.admitted = admitted;
    it.round = m_round;
    return it;
}

void ReviewQueueModel::rebuildPos()
{
    m_pos.clear();
    m_pos.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) m_pos.insert(m_items[i].id, i);
}

void ReviewQueueModel::setItems(QVector<Item> items)
{
    beginResetModel();
    m_items = std::move(items);
    for (Item& it : m_items) {
        it.state = stateOf(it);
        it.sig = sigOf(it.id);
    }
    m_current = -1;
    rebuildPos();
    endResetModel();
}

void ReviewQueueModel::updateIncoming()
{
    QStringList inc;
    for (auto it = m_doubt.cbegin(); it != m_doubt.cend(); ++it) {
        if (m_pos.contains(it.key())) continue;
        const EditorUtterance* u = utt(it.key());
        if (u && !flagged(*u)) inc << it.key();
    }
    sortByTime(inc);
    if (inc == m_incoming) return;
    m_incoming = inc;
    emit incomingChanged();
}

void ReviewQueueModel::beginRoundBaseline()
{
    m_roundStartDoubt.clear();
    for (auto it = m_doubt.cbegin(); it != m_doubt.cend(); ++it) m_roundStartDoubt.insert(it.key());
    m_roundStartSamples.clear();
    for (const QVariant& v : m_vm->sampleStats()) {
        const QVariantMap m = v.toMap();
        m_roundStartSamples.insert(m.value(QStringLiteral("key")).toString(),
                                   m.value(QStringLiteral("sessionAdded")).toInt());
    }
    m_log.clear();
    m_dismissed.clear();
}

QSet<QString> ReviewQueueModel::grownSpeakers() const
{
    QSet<QString> out;
    for (const QVariant& v : m_vm->sampleStats()) {
        const QVariantMap m = v.toMap();
        const QString key = m.value(QStringLiteral("key")).toString();
        if (m.value(QStringLiteral("sessionAdded")).toInt() > m_roundStartSamples.value(key)) out.insert(key);
    }
    return out;
}

bool ReviewQueueModel::causedByDecisions(const QString& id, const QSet<QString>& grown) const
{
    if (grown.isEmpty() || m_roundStartDoubt.contains(id)) return false;
    const EditorUtterance* u = utt(id);
    if (u && grown.contains(u->speakerKey)) return true;
    const auto d = m_doubt.constFind(id);
    if (d != m_doubt.cend() && !d->proposedKey.isEmpty() && grown.contains(d->proposedKey)) return true;
    return u && !u->likelySpeakerKey.isEmpty() && grown.contains(u->likelySpeakerKey);
}

void ReviewQueueModel::computeOpeners(const QVector<TranscriptEditorViewModel::ReviewView>& views)
{
    m_openers.clear();
    for (const auto& v : views) {
        if (!v.actionable || v.kind == QLatin1String("uncertain")) continue;
        if (v.proposedKey.isEmpty() && v.proposedName.isEmpty() && v.newSide.isEmpty()) continue;
        QStringList ids;
        for (const QString& id : v.ids) {
            const int r = m_pos.value(id, -1);
            if (r >= 0 && m_items[r].state == Pending) ids << id;
        }
        if (ids.size() < m_openerMinLines) continue;
        double voice = -1.0;
        bool manual = false;
        for (const QVariant& e : v.evidence) {
            const QVariantMap m = e.toMap();
            const QString kind = m.value(QStringLiteral("kind")).toString();
            const QString pol = m.value(QStringLiteral("polarity")).toString();
            if (kind == QLatin1String("voice") && pol == QLatin1String("support"))
                voice = std::max(voice, m.value(QStringLiteral("value")).toDouble());
            if (kind == QLatin1String("side") && pol == QLatin1String("contradict")
                && m.value(QStringLiteral("manual")).toBool() && v.kind == QLatin1String("sideConflict"))
                manual = true;
        }
        if (voice < m_openerMinVoice && !manual) continue;
        sortByTime(ids);
        Opener o;
        o.view = Doubt{v.id, v.kind, v.title, v.evidence, v.proposedKey, v.proposedName, v.newSide};
        o.currentKey = v.currentKey;
        o.ids = ids;
        o.voicePct = voice < 0.0 ? -1 : int(std::lround(voice * 100.0));
        o.manualSide = manual;
        m_openers.append(o);
    }
    // Nagyobb csoport előbb.
    std::stable_sort(m_openers.begin(), m_openers.end(),
                     [](const Opener& a, const Opener& b) { return a.ids.size() > b.ids.size(); });
}

// ---- munkalista -------------------------------------------------------------

void ReviewQueueModel::start()
{
    m_vm->flushPendingReview();
    startFrom(m_vm->reviewViews(), m_vm->utterances());
}

void ReviewQueueModel::startFrom(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                                 const QVector<EditorUtterance>& utterances)
{
    m_round = 1;
    m_doubt = computeDoubt(views, utterances);
    QStringList ids = m_doubt.keys();
    sortByTime(ids);
    QVector<Item> items;
    items.reserve(ids.size());
    for (const QString& id : std::as_const(ids)) items.append(makeItem(id, m_doubt.value(id), false));
    const bool wasActive = m_active;
    m_active = true;
    setItems(std::move(items));
    beginRoundBaseline();
    computeOpeners(views);
    m_incoming.clear();
    updateIncoming();
    setCurrent(nextOpen(0), true);
    if (!wasActive) emit activeChanged();
    emit roundChanged();
    emit countsChanged();
    emit incomingChanged();
    emit patternChanged();
}

void ReviewQueueModel::stop()
{
    const bool was = m_active;
    m_active = false;
    beginResetModel();
    m_items.clear();
    m_pos.clear();
    m_current = -1;
    endResetModel();
    m_doubt.clear();
    m_incoming.clear();
    m_roundStartDoubt.clear();
    m_roundStartSamples.clear();
    m_log.clear();
    m_dismissed.clear();
    m_openers.clear();
    m_round = 1;
    if (was) emit activeChanged();
    emit currentChanged();
    emit countsChanged();
    emit incomingChanged();
    emit patternChanged();
}

void ReviewQueueModel::startNextRound(bool includeIncoming, bool includeLater)
{
    if (!m_active) {
        start();
        return;
    }
    const QSet<QString> grown = grownSpeakers();
    QHash<QString, Item> carry;
    QStringList ids;
    for (const Item& it : std::as_const(m_items)) {
        if (it.state == Pending || (includeLater && it.state == Later)) {
            Item c = it;
            c.later = false;
            c.suggestionUpdated = false;
            carry.insert(c.id, c);
            ids << c.id;
        }
    }
    QSet<QString> fresh;
    if (includeIncoming) {
        for (const QString& id : std::as_const(m_incoming)) {
            if (carry.contains(id)) continue;
            ids << id;
            fresh.insert(id);
        }
    }
    sortByTime(ids);
    ++m_round;
    QVector<Item> items;
    items.reserve(ids.size());
    for (const QString& id : std::as_const(ids)) {
        if (fresh.contains(id)) {
            Item it = makeItem(id, m_doubt.value(id), true);
            it.consequence = causedByDecisions(id, grown);
            items.append(it);
        } else {
            Item it = carry.value(id);
            // A javaslat a mostani elemzésé.
            const auto d = m_doubt.constFind(id);
            if (d != m_doubt.cend()) {
                it.likelyKey = d->proposedKey;
                it.likelyName = d->proposedName;
                it.likelyNewSide = d->newSide;
            }
            it.round = m_round;
            items.append(it);
        }
    }
    setItems(std::move(items));
    beginRoundBaseline();
    computeOpeners(m_vm->reviewViews());
    updateIncoming();
    setCurrent(nextOpen(0), true);
    emit roundChanged();
    emit countsChanged();
    emit incomingChanged();
    emit patternChanged();
}

int ReviewQueueModel::admitIncoming()
{
    if (!m_active || m_incoming.isEmpty()) return 0;
    const QSet<QString> grown = grownSpeakers();
    QStringList add = m_incoming;
    sortByTime(add);
    const int first = int(m_items.size());
    beginInsertRows(QModelIndex(), first, first + int(add.size()) - 1);
    for (const QString& id : std::as_const(add)) {
        Item it = makeItem(id, m_doubt.value(id), true);
        it.consequence = causedByDecisions(id, grown);
        it.state = stateOf(it);
        it.sig = sigOf(id);
        m_items.append(it);
    }
    rebuildPos();
    endInsertRows();
    updateIncoming();
    if (m_current < 0) setCurrent(nextOpen(first), true);
    emit countsChanged();
    emit patternChanged();
    return int(add.size());
}

// ---- kurzor -----------------------------------------------------------------

void ReviewQueueModel::setCurrent(int index, bool force)
{
    if (index < -1 || index >= m_items.size()) index = -1;
    if (index == m_current && !force) return;
    const int old = m_current;
    m_current = index;
    if (old >= 0 && old < m_items.size() && old != index) emit dataChanged(this->index(old), this->index(old), {CurrentRole});
    if (index >= 0) emit dataChanged(this->index(index), this->index(index), {CurrentRole});
    emit currentChanged();
    emit patternChanged();      // a sameAhead a kurzor sorát kihagyja
}

int ReviewQueueModel::nextOpen(int from) const
{
    const int n = int(m_items.size());
    from = std::max(0, from);
    for (int k = from; k < n; ++k)
        if (isOpen(m_items[k])) return k;
    for (int k = 0; k < std::min(from, n); ++k)
        if (isOpen(m_items[k])) return k;
    return -1;
}

bool ReviewQueueModel::next()
{
    if (m_current < 0 || m_current + 1 >= m_items.size()) return false;
    setCurrent(m_current + 1);
    return true;
}

bool ReviewQueueModel::previous()
{
    if (m_items.isEmpty()) return false;
    if (m_current < 0) {
        setCurrent(int(m_items.size()) - 1);
        return true;
    }
    if (m_current == 0) return false;
    setCurrent(m_current - 1);
    return true;
}

bool ReviewQueueModel::advance()
{
    setCurrent(nextOpen(m_current + 1));
    return m_current >= 0;
}

bool ReviewQueueModel::retreat()
{
    const int n = int(m_items.size());
    if (n == 0) return false;
    const int from = m_current >= 0 ? m_current : n;
    for (int step = 1; step <= n; ++step) {
        const int k = ((from - step) % n + n) % n;
        if (k == m_current) break;
        if (isOpen(m_items[k])) {
            setCurrent(k);
            return true;
        }
    }
    return false;
}

int ReviewQueueModel::nextOpenIndex() const
{
    const int k = nextOpen(m_current + 1);
    return k == m_current ? -1 : k;
}

QStringList ReviewQueueModel::confirmableIds() const
{
    QStringList out;
    for (const Item& it : m_items) {
        if (it.state != AlreadyDecided) continue;
        const EditorUtterance* u = utt(it.id);
        if (u && !u->ignored && u->endMs - u->startMs >= tanara::speakeredit::kMinEmbedMs) out << it.id;
    }
    return out;
}

bool ReviewQueueModel::jumpTo(int index)
{
    if (index < 0 || index >= m_items.size()) return false;
    setCurrent(index);
    return true;
}

bool ReviewQueueModel::later()
{
    if (m_current < 0 || m_current >= m_items.size()) return false;
    const int cur = m_current;
    Item& it = m_items[cur];
    if (it.state == AlreadyDecided) {
        // A magától eldőlt sor nem nyitott: tovább a következő nyitottra (a sor a helyén marad).
        setCurrent(nextOpen(cur + 1), true);    // nincs több nyitott: -1 (kör vége)
        return true;
    }
    if (it.state != Pending) return next();
    it.later = true;
    it.state = Later;
    m_log.append({it.id, QString()});
    const int last = int(m_items.size()) - 1;
    if (cur != last) {
        beginMoveRows(QModelIndex(), cur, cur, QModelIndex(), last + 1);
        m_items.append(m_items.takeAt(cur));
        endMoveRows();
        rebuildPos();
        emit dataChanged(index(cur), index(last), {OrderRole, CurrentRole});
    } else {
        emitRow(cur);
    }
    m_current = -1;     // a kurzor sora elmozdult: mindenképp jelezzük
    setCurrent(nextOpen(cur), true);
    emit countsChanged();
    return true;
}

int ReviewQueueModel::bringToFront(const QStringList& utteranceIds)
{
    if (!m_active) return 0;
    QSet<QString> want;
    for (const QString& id : utteranceIds) {
        const int r = m_pos.value(id, -1);
        if (r >= 0 && isOpen(m_items[r])) want.insert(id);
    }
    if (want.isEmpty()) return 0;
    const int pos = m_current >= 0 ? m_current : 0;
    QVector<Item> moved, rest;
    int before = 0;     // a pos előtti, elmozduló sorok
    for (int i = 0; i < m_items.size(); ++i) {
        if (want.contains(m_items[i].id)) {
            moved.append(m_items[i]);
            if (i < pos) ++before;
        } else {
            rest.append(m_items[i]);
        }
    }
    const int at = std::clamp(pos - before, 0, int(rest.size()));
    beginResetModel();
    m_items = rest;
    for (int i = 0; i < moved.size(); ++i) m_items.insert(at + i, moved[i]);
    rebuildPos();
    m_current = -1;
    endResetModel();
    setCurrent(at, true);
    emit countsChanged();
    emit patternChanged();
    return int(moved.size());
}

// ---- szinkron ---------------------------------------------------------------

void ReviewQueueModel::emitRow(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    emit dataChanged(index(row), index(row));
    if (row == m_current) emit currentChanged();
}

void ReviewQueueModel::syncFromReview()
{
    syncFromReview(m_vm->reviewViews(), m_vm->utterances());
}

void ReviewQueueModel::syncFromReview(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                                      const QVector<EditorUtterance>& utterances)
{
    if (!m_active) return;
    m_doubt = computeDoubt(views, utterances);
    bool anyChange = false;
    for (int r = 0; r < m_items.size(); ++r) {
        Item& it = m_items[r];
        bool changed = false;
        if (!isDecidedState(it.state)) {
            const auto d = m_doubt.constFind(it.id);
            if (d != m_doubt.cend()
                && (d->proposedKey != it.likelyKey || d->proposedName != it.likelyName || d->newSide != it.likelyNewSide)) {
                it.likelyKey = d->proposedKey;
                it.likelyName = d->proposedName;
                it.likelyNewSide = d->newSide;
                it.suggestionUpdated = true;
                changed = true;
                emit suggestionUpdated(it.id);
            }
        }
        const State s = stateOf(it);
        if (s != it.state) {
            it.state = s;
            changed = true;
        }
        if (changed) {
            anyChange = true;
            emitRow(r);
        }
    }
    updateIncoming();
    if (anyChange) {
        emit countsChanged();
        emit patternChanged();
    }
}

QString ReviewQueueModel::patternKey(const Item& it, const QString& targetKey) const
{
    return it.reason.kind + QChar(0x1f) + it.originalKey + QChar(0x1f) + targetKey;
}

void ReviewQueueModel::refreshUtterances(const QVector<int>& utteranceIndices)
{
    if (!m_active || m_items.isEmpty()) return;
    bool counts = false, pattern = false, advanceCursor = false;
    int undoneRow = -1, undoneOpenRow = -1, undoneCount = 0;
    for (int ui : utteranceIndices) {
        if (ui < 0 || ui >= m_vm->utterances().size()) continue;
        const QString id = m_vm->utterances()[ui].id;
        const int r = m_pos.value(id, -1);
        if (r < 0) continue;
        Item& it = m_items[r];
        const State old = it.state;
        const QString sig = sigOf(id);
        const State s = stateOf(it);
        if (sig != it.sig) {
            // Csak a „nem minta" jelző változott (pl. az Imént sor „Legyen nem minta"): nem új döntés.
            const bool noisyOnly = !sig.isEmpty() && !it.sig.isEmpty() && sig.chopped(1) == it.sig.chopped(1);
            it.sig = sig;
            if (noisyOnly && isDecidedState(s) && isDecidedState(old)) {
                // a napló és a kurzor marad
            } else if (isDecidedState(s)) {
                // Egy döntés (vagy egy döntött sor újradöntése): a napló végére.
                m_log.erase(std::remove_if(m_log.begin(), m_log.end(), [&](const PatternEntry& e) { return e.id == id; }),
                            m_log.end());
                const EditorUtterance& u = m_vm->utterances()[ui];
                const bool moved = s == Decided && u.manuallyCorrected && u.speakerKey != it.originalKey;
                m_log.append({id, moved ? patternKey(it, u.speakerKey) : QString()});
                it.suggestionUpdated = false;
                it.later = false;
                pattern = true;
                if (r == m_current) advanceCursor = true;
            } else if (isDecidedState(old)) {
                // Visszavont döntés: a sor újra nyitott, a kurzor rá ugrik.
                m_log.erase(std::remove_if(m_log.begin(), m_log.end(), [&](const PatternEntry& e) { return e.id == id; }),
                            m_log.end());
                pattern = true;
                undoneRow = undoneRow < 0 ? r : std::min(undoneRow, r);
                ++undoneCount;
                if (s == Pending) undoneOpenRow = undoneOpenRow < 0 ? r : std::min(undoneOpenRow, r);
            }
        }
        if (s != old) {
            it.state = s;
            counts = true;
        }
        emitRow(r);
    }
    updateIncoming();
    // Visszavonás: a kurzor a visszaállt nyitott sorra; egy magától eldőlt sorra csak akkor, ha
    // egyetlen sor állt vissza (a „Mind megerősítem” visszavonása nem rángatja a kurzort).
    if (undoneOpenRow >= 0) setCurrent(undoneOpenRow);
    else if (undoneRow >= 0 && undoneCount == 1) setCurrent(undoneRow);
    else if (advanceCursor) setCurrent(nextOpen(m_current + 1));
    if (counts) emit countsChanged();
    if (pattern) emit patternChanged();
}

void ReviewQueueModel::refreshAll()
{
    if (!m_items.isEmpty()) emit dataChanged(index(0), index(int(m_items.size()) - 1));
    emit currentChanged();
}

// ---- összesítők -------------------------------------------------------------

QVariantMap ReviewQueueModel::roundSummary() const
{
    QVariantList delta;
    struct D { QString key; int d; };
    QVector<D> ds;
    for (const QVariant& v : m_vm->sampleStats()) {
        const QVariantMap m = v.toMap();
        const QString key = m.value(QStringLiteral("key")).toString();
        const int d = m.value(QStringLiteral("sessionAdded")).toInt() - m_roundStartSamples.value(key);
        if (d != 0) ds.append({key, d});
    }
    std::stable_sort(ds.begin(), ds.end(), [](const D& a, const D& b) { return a.d > b.d; });
    for (const D& d : std::as_const(ds)) {
        const auto view = m_vm->speakerView(d.key);
        delta << QVariantMap{{QStringLiteral("key"), d.key},
                             {QStringLiteral("name"), view.name},
                             {QStringLiteral("colorIndex"), view.colorIndex},
                             {QStringLiteral("delta"), d.d}};
    }
    int maxIndex = -1;
    for (const Item& it : m_items) maxIndex = std::max(maxIndex, m_vm->utteranceIndexOf(it.id));
    const QSet<QString> grown = grownSpeakers();
    int inReviewed = 0;
    QStringList caused;
    for (const QString& id : m_incoming) {
        if (m_vm->utteranceIndexOf(id) <= maxIndex) ++inReviewed;
        if (causedByDecisions(id, grown)) caused << id;
    }
    return {
        {QStringLiteral("round"), m_round},
        {QStringLiteral("total"), count()},
        {QStringLiteral("decided"), decidedCount()},
        {QStringLiteral("ignored"), ignoredCount()},
        {QStringLiteral("later"), laterCount()},
        {QStringLiteral("alreadyDecided"), alreadyDecidedCount()},
        {QStringLiteral("confirmable"), confirmableCount()},
        {QStringLiteral("open"), openCount()},
        {QStringLiteral("sampleDelta"), delta},
        {QStringLiteral("incoming"), incomingCount()},
        {QStringLiteral("incomingIds"), m_incoming},
        {QStringLiteral("incomingInReviewed"), inReviewed},
        {QStringLiteral("incomingCausedByDecisions"), int(caused.size())},
        {QStringLiteral("causedIds"), caused},
    };
}

QVariantMap ReviewQueueModel::recentPattern() const
{
    if (!m_active || m_log.isEmpty()) return {};
    const int from = std::max(0, int(m_log.size()) - kPatternWindow);
    QHash<QString, int> hits;
    QHash<QString, int> lastAt;
    for (int i = from; i < m_log.size(); ++i) {
        const QString& k = m_log[i].key;
        if (k.isEmpty() || m_dismissed.contains(k)) continue;
        ++hits[k];
        lastAt[k] = i;
    }
    QString best;
    for (auto it = hits.cbegin(); it != hits.cend(); ++it) {
        if (best.isEmpty() || it.value() > hits.value(best)
            || (it.value() == hits.value(best) && lastAt.value(it.key()) > lastAt.value(best)))
            best = it.key();
    }
    if (best.isEmpty() || hits.value(best) < kPatternMinHits) return {};
    const QString reason = best.section(QChar(0x1f), 0, 0);
    const QString cur = best.section(QChar(0x1f), 1, 1);
    const QString target = best.section(QChar(0x1f), 2, 2);
    const QString targetName = m_vm->speakerView(target).name;
    const QString targetPerson = m_vm->personOf(target);
    auto matches = [&](const Item& it) {
        if (it.state != Pending || it.reason.kind != reason) return false;
        const EditorUtterance* u = utt(it.id);
        if (!u || u->speakerKey != cur) return false;
        if (!it.likelyKey.isEmpty()) return it.likelyKey == target;
        return !it.likelyName.isEmpty() && (it.likelyName == targetPerson || it.likelyName == targetName);
    };
    QStringList ahead;
    for (int r = 0; r < m_items.size(); ++r)
        if (r != m_current && matches(m_items[r])) ahead << m_items[r].id;
    const bool currentMatches = m_current >= 0 && m_current < m_items.size() && matches(m_items[m_current]);
    return {
        {QStringLiteral("key"), best},
        {QStringLiteral("reasonKind"), reason},
        {QStringLiteral("currentKey"), cur},
        {QStringLiteral("currentName"), m_vm->speakerView(cur).name},
        {QStringLiteral("proposedKey"), target},
        {QStringLiteral("proposedName"), targetName},
        {QStringLiteral("hits"), hits.value(best)},
        {QStringLiteral("window"), int(m_log.size()) - from},
        {QStringLiteral("sameAhead"), ahead},
        {QStringLiteral("currentMatches"), currentMatches},
    };
}

void ReviewQueueModel::dismissPattern(const QString& key)
{
    if (key.isEmpty() || m_dismissed.contains(key)) return;
    m_dismissed.insert(key);
    emit patternChanged();
}

QVariantList ReviewQueueModel::openers() const
{
    QVariantList out;
    for (const Opener& o : m_openers) {
        int pending = 0, samples = 0;
        for (const QString& id : o.ids) {
            const int r = m_pos.value(id, -1);
            if (r < 0 || m_items[r].state != Pending) continue;
            ++pending;
            // Minta-becslés (a core minta-szabálya): elég hosszú, nem zajos sor.
            const EditorUtterance* u = utt(id);
            if (u && !u->noisy && u->endMs - u->startMs >= tanara::speakeredit::kMinEmbedMs) ++samples;
        }
        const int n = int(o.ids.size());
        QStringList spot;
        for (int k : {n / 6, n / 2, (5 * n) / 6}) {
            const QString id = o.ids.value(std::clamp(k, 0, n - 1));
            if (!spot.contains(id)) spot << id;
        }
        QString subtitle;
        for (const auto& v : m_vm->reviewViews())
            if (v.id == o.view.groupId) subtitle = v.subtitle;
        const QString proposedName = !o.view.proposedKey.isEmpty() && !m_vm->speakerView(o.view.proposedKey).name.isEmpty()
            ? m_vm->speakerView(o.view.proposedKey).name : o.view.proposedName;
        out << QVariantMap{
            {QStringLiteral("groupId"), o.view.groupId},
            {QStringLiteral("kind"), o.view.kind},
            {QStringLiteral("title"), o.view.title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("evidence"), o.view.evidence},
            {QStringLiteral("ids"), o.ids},
            {QStringLiteral("count"), n},
            {QStringLiteral("pendingCount"), pending},
            {QStringLiteral("sampleCount"), samples},
            {QStringLiteral("currentKey"), o.currentKey},
            {QStringLiteral("currentName"), m_vm->speakerView(o.currentKey).name},
            {QStringLiteral("proposedKey"), o.view.proposedKey},
            {QStringLiteral("proposedName"), proposedName},
            {QStringLiteral("newSide"), o.view.newSide},
            {QStringLiteral("voicePct"), o.voicePct},
            {QStringLiteral("manualSide"), o.manualSide},
            {QStringLiteral("spotIds"), spot},
        };
    }
    return out;
}

} // namespace tanara_qml
