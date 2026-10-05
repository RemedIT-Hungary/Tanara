#pragma once
//
// TagInputModel — a címke-beviteli mező (C02, TagInput.qml) felugró listája.
//
// text → rows: üres mezőn a legutóbb használt címkék, gépeléskor a találatok (ékezet- és
// kisbetű-független, előbb a név eleje, aztán a szókezdet, aztán bárhol) + „Új címke: „…””,
// nagyon hasonló névnél „HASONLÓ MÁR VAN” + „Mégis új: „…”” (szabályok: TagMatching.h).
// A kijelölés a nyilakkal mozog; Enter = chooseSelected() → chosen(név, új-e).
//
// Adatforrás: TagBackend (controller nélkül a kitalált 12 címke). A `controller` property a
// wave 2 bekötési pontja; tesztben setBackend()-del ál-backend adható.
//
#include "TagMatching.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TagBackend;

class TagInputModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // [{ kind, id, name, count, matchStart, matchLen, before, match, after }]
    //   kind: "recent" | "match" | "new" | "nearDuplicate" | "forceNew"
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    // "recent" (üres mező) | "typing" | "similar" (nagyon hasonló név van) — a fejléchez
    Q_PROPERTY(QString mode READ mode NOTIFY rowsChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged)
    // Legfeljebb ennyi meglévő címke (5; a felvevő kompakt listájában 3).
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    // A megbeszélésen már rajta lévő címkék (ezeket nem kínálja).
    Q_PROPERTY(QStringList excludeIds READ excludeIds WRITE setExcludeIds NOTIFY excludeIdsChanged)

public:
    explicit TagInputModel(QObject* parent = nullptr);

    QObject* controller() const { return m_controller; }
    void setController(QObject* controller);
    // Tesztekhez / közös backendhez: nem veszi át a tulajdonjogot.
    void setBackend(TagBackend* backend);
    TagBackend* backend() const { return m_backend; }

    QString text() const { return m_text; }
    void setText(const QString& text);
    QVariantList rows() const { return m_rowsVariant; }
    QString mode() const;
    int selectedRow() const { return m_selected; }
    void setSelectedRow(int row);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    QStringList excludeIds() const { return m_exclude; }
    void setExcludeIds(const QStringList& ids);

    const QVector<tagmatch::InputRow>& rowData() const { return m_rows; }

    // A kijelölés léptetése (körbe nem fordul).
    Q_INVOKABLE void move(int delta);
    // A kijelölt sor választása: chosen(név, új-e). false, ha nincs mit választani.
    Q_INVOKABLE bool chooseSelected();
    Q_INVOKABLE bool choose(int row);

signals:
    void controllerChanged();
    void textChanged();
    void rowsChanged();
    void selectedRowChanged();
    void limitChanged();
    void excludeIdsChanged();
    void chosen(const QString& name, bool isNew);

private:
    void attach(TagBackend* backend);
    void rebuild();

    QPointer<QObject> m_controller;
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QMetaObject::Connection m_conn;
    QString m_text;
    QVector<tagmatch::InputRow> m_rows;
    QVariantList m_rowsVariant;
    int m_selected = -1;
    int m_limit = 5;
    QStringList m_exclude;
};

} // namespace tanara_qml
