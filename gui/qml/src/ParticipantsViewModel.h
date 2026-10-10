#pragma once
//
// ParticipantsViewModel — a „Ki volt ott?" párbeszéd (ParticipantsDialog.qml, handoff-v3 V2)
// nézetmodellje. A meeting résztvevő-jelöltjeit (AppController::participants) csoportokra bontja
// (Biztos / Kétséges / Meghívott, de nem hallottuk + a kézzel felvett, még nem hallottak), és egy
// MUNKAPÉLDÁNYON tartja a jelöléseket: a bejelölés, a hozzáadás és az elnevezés csak a
// „Tovább"-ra (approve → AppController::approveParticipants) kerül a meetingre; a „Kihagyás"
// (skip → skipApproval) kötés nélkül dönt.
//
// Alapjelölés: ha már volt jóváhagyás (nem kihagyás), az akkori döntés; különben
// participants::defaultChecked (névvel bír és nincs ellentmondó bizonyítéka); a meghívott, de
// nem hallott jelölt alapból ki.
//
// „Ő nem volt ott" (az átirat-szerkesztőből): unbind(participantId) → unbindParticipant.
//
// Controller nélkül vagy App.demo mellett KITALÁLT mintaadat (a handoff nevei); a demoState
// választ: allSure | doubts | invitedNotHeard | addPerson | running | noTranscriptYet.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class ParticipantsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Alapból az App-singleton controllere; tesztben injektálható.
    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    // Fut-e a hangelemzés (a párbeszéd „fut" állapota).
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    // Vár-e jóváhagyásra (a banner / az automatikus megnyitás feltétele).
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
    // Mono meta a címsor jobb oldalán: „hanglenyomat-elemzés · CAM++ + WeSpeaker · 3 perce".
    Q_PROPERTY(QString metaText READ metaText NOTIFY changed)
    // [{ key: sure|doubt|invited|manual, label, hint, count, rows: [row] }] — üres csoport nincs benne.
    // row: { id, name, displayName, colorIndex, checked, checkable, anonymous, added,
    //        evidence: [{ kind, polarity, text, side }], mapping, mappingPending, mappingDoubt }
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY changed)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY changed)
    // A lábléc információs sora (a kötetlen nyers beszélők névtelenek maradnak).
    Q_PROPERTY(QString infoText READ infoText NOTIFY changed)
    // Címke-alapú javaslat: „A #Nordvik alapján:" + [{ name, countText }] (legfeljebb 3).
    Q_PROPERTY(QString tagSuggestionLabel READ tagSuggestionLabel NOTIFY changed)
    Q_PROPERTY(QVariantList tagSuggestions READ tagSuggestions NOTIFY changed)

public:
    explicit ParticipantsViewModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    bool running() const { return m_running; }
    bool hasTranscript() const { return m_hasTranscript; }
    bool approvalPending() const { return m_pending; }
    QString metaText() const { return m_metaText; }
    QVariantList groups() const;
    int rowCount() const { return int(m_rows.size()); }
    int checkedCount() const;
    QString infoText() const;
    QString tagSuggestionLabel() const { return m_tagLabel; }
    QVariantList tagSuggestions() const;

    // A személyválasztóhoz (PersonListModel.participants): ismert személyek, és hogy valaki
    // már szerepel-e a párbeszédben.
    QVector<tanara::PersonInfo> people() const;
    bool isMeetingPerson(const QString& name) const;

    // Újraolvasás a controllerből; a munkapéldány (jelölések, hozzáadások, elnevezések) eldobva.
    // A párbeszéd megnyitásakor hívódik.
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setChecked(const QString& id, bool checked);
    // Új résztvevő a párbeszédből (még nem mentve; a „Tovább" viszi a meetingre). Ha már
    // szerepel, azt jelöli be. Visszaad: a sor id-ja.
    Q_INVOKABLE QString addPerson(const QString& name);
    // A címke-alapú javaslat elfogadása (index a tagSuggestions-ben).
    Q_INVOKABLE void acceptTagSuggestion(int index);
    // Névtelen (ismeretlen hangú) sor elnevezése; bejelöli.
    Q_INVOKABLE void nameRow(const QString& id, const QString& name);
    // A párbeszédben felvett sor elvetése.
    Q_INVOKABLE void removeAdded(const QString& id);
    // „Tovább": a bejelöltek jóváhagyása. false: nincs meeting / a controller elutasította.
    Q_INVOKABLE bool approve();
    // „Kihagyás": döntés kötés nélkül.
    Q_INVOKABLE bool skip();
    // „Ő nem volt ott": a résztvevő sorai névtelenek lesznek, a jelölése törlődik.
    Q_INVOKABLE bool unbind(const QString& participantId);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void changed();

private:
    struct Row {
        QString id;
        QString name;                   // üres = névtelen (ismeretlen hang)
        int group = 1;                  // 0 biztos, 1 kétséges, 2 meghívott, 3 hozzáadva
        bool checked = false;
        bool added = false;             // a párbeszédben felvett (még nincs a meetingen)
        bool heard = false;             // hallottuk (van hang-klasztere / oldala)
        bool manual = false;            // kézzel felvett (a párbeszédben vagy korábban)
        QVector<tanara::Evidence> evidence;
        QStringList rawIds;
        QStringList sides;
        int colorIndex = 0;
    };

    tanara::AppController* app() const;
    void connectController();
    void load();
    void loadDemo();
    void reloadTagSuggestions();
    QVariantMap rowMap(const Row& r) const;
    bool rawBoundToChecked(const QString& rawLabel, const QString& exceptId) const;
    int indexOf(const QString& id) const;
    int indexOfName(const QString& name) const;
    QString nextAddedId();

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    bool m_running = false;
    bool m_hasTranscript = false;
    bool m_pending = false;
    QString m_metaText;
    QString m_selfName;
    QStringList m_rawLabels;            // az átirat nyers beszélői (átirat nélkül üres)
    QStringList m_tagIds;
    QVector<Row> m_rows;
    int m_addedSeq = 0;

    struct TagSuggestion { QString name; QString tagName; int shared = 0; int total = 0; };
    QString m_tagLabel;
    QVector<TagSuggestion> m_tagSuggestions;
    QString m_demoInfo;                 // a demó lábléc-sora (ha nem számolható)
    QVector<tanara::PersonInfo> m_demoPeople;
};

} // namespace tanara_qml
