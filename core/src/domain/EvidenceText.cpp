#include "tanara/domain/EvidenceText.h"

#include "tanara/domain/EvidenceKeys.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace tanara {

namespace {

using namespace evidencekeys;

QString percent(double v) { return QString::number(qRound(std::clamp(v, 0.0, 1.0) * 100.0)); }

QString sideName(const QString& side)
{
    if (side == QLatin1String("mic")) return QCoreApplication::translate("EvidenceText", "mikrofon");
    if (side == QLatin1String("loopback")) return QCoreApplication::translate("EvidenceText", "hívás");
    return {};
}

QString sidesText(const QStringList& sides)
{
    QStringList labels;
    for (const QString& s : sides) labels << sideName(s);
    return labels.join(QCoreApplication::translate("EvidenceText", " és "));
}

QString hashed(const QString& name) { return QStringLiteral("#") + name; }

struct TagItem { QString name; int shared = 0; int total = 0; bool manual = false; };

QVector<TagItem> tagItems(const Evidence& e)
{
    QVector<TagItem> out;
    for (const QVariant& v : e.params.value(kParamItems).toList()) {
        const QVariantMap m = v.toMap();
        out.append({m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("shared")).toInt(),
                    m.value(QStringLiteral("total")).toInt(), m.value(QStringLiteral("manual")).toBool()});
    }
    return out;
}

} // namespace

QString evidenceText(const Evidence& e)
{
    if (e.key.isEmpty()) return e.text;
    if (e.key == kVoiceScore)        return QCoreApplication::translate("EvidenceText", "hang %1%").arg(percent(e.value));
    if (e.key == kVoiceUnknown)      return QCoreApplication::translate("EvidenceText", "ismeretlen hang");
    if (e.key == kVoiceNotHeard)     return QCoreApplication::translate("EvidenceText", "nem hallottuk");
    if (e.key == kVoiceNoVoiceprint) return QCoreApplication::translate("EvidenceText", "nincs lenyomata");
    if (e.key == kSideHeard)         return sidesText(e.params.value(kParamSides).toStringList());
    if (e.key == kSideCallOnly)      return QCoreApplication::translate("EvidenceText", "csak a hívás hangján");
    if (e.key == kManualTracks)      return QCoreApplication::translate("EvidenceText", "kézzel: %1").arg(sidesText(e.params.value(kParamSides).toStringList()));
    if (e.key == kManualAdded)       return QCoreApplication::translate("EvidenceText", "kézzel felvéve");
    if (e.key == kTagMatch)          return e.params.value(kParamChip).toString();
    if (e.key == kTagOther)          return QCoreApplication::translate("EvidenceText", "más címkék");
    if (e.key == kLegacy)            return e.params.value(kParamText).toString();
    return e.text;   // an unknown key (a newer file): whatever text came with it
}

QString evidenceDetail(const Evidence& e)
{
    const QVector<TagItem> items = (e.key == kTagMatch || e.key == kTagOther) ? tagItems(e) : QVector<TagItem>();
    if (items.isEmpty()) return e.detail;
    const TagItem& f = items.first();
    if (e.key == kTagMatch) {
        const QString person = e.params.value(kParamPerson).toString().trimmed();
        QString d = f.manual && !person.isEmpty()
            ? QCoreApplication::translate("EvidenceText", "%1 címkéje: %2 (%3 / %4 megbeszélésen volt ott).").arg(person, hashed(f.name)).arg(f.shared).arg(f.total)
            : QCoreApplication::translate("EvidenceText", "A %1 megbeszéléseken szokott lenni (%2 / %3).").arg(hashed(f.name)).arg(f.shared).arg(f.total);
        if (items.size() > 1) {
            QStringList all;
            for (const TagItem& it : items)
                all << QStringLiteral("%1 %2 / %3").arg(hashed(it.name)).arg(it.shared).arg(it.total);
            d += QLatin1Char(' ') + QCoreApplication::translate("EvidenceText", "Közös címkék: %1.").arg(all.join(QStringLiteral(", ")));
        }
        return d;
    }
    QStringList meeting;
    for (const TagItem& it : items) meeting << hashed(it.name);
    QStringList other;
    for (const QString& n : e.params.value(kParamElsewhere).toStringList()) other << hashed(n);
    const QString none = items.size() == 1
        ? QCoreApplication::translate("EvidenceText", "A %1 %2 megbeszéléséből egyiken sem volt.").arg(meeting.first()).arg(f.total)
        : QCoreApplication::translate("EvidenceText", "A %1 megbeszélésein egyiken sem volt.").arg(meeting.join(QStringLiteral(", ")));
    return other.isEmpty() ? none
                           : QCoreApplication::translate("EvidenceText", "Más címkéken szokott lenni: %1.").arg(other.join(QStringLiteral(", "))) + QLatin1Char(' ') + none;
}

QStringList evidenceSides(const Evidence& e)
{
    if (e.key == kSideCallOnly) return {QStringLiteral("loopback")};
    if (e.key == kSideHeard || e.key == kManualTracks) return e.params.value(kParamSides).toStringList();
    return {};
}

} // namespace tanara
