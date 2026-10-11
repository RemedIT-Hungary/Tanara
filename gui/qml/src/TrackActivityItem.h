#pragma once
//
// Tanara QML — a beszéd-csík rajza (TrackActivityStrip): egy sáv idővonala a megbeszélés
// teljes hosszán, képpont-oszloponként: nincs adat → üres; csend → vékony alapvonal; beszéd →
// teljes magasságú sáv (az oszlop beszédes kereteinek arányában telítve). A TrackActivityModel
// kereteiből C++-ban számol (nincs QML-tömb), az oszlopokat szélesség / adat-változásig
// gyorsítótárazza — 2 óra × 50 ms-os keretsor is gyors. A lejátszás-fej és az egér-kezelés a
// QML-ben van (nem rajzol újra mozgáskor).
//   TrackActivityItem { model: activity; trackId: "mic"; color: Theme.speakerLine(0) }
// Nincs adat (számol / nincs ilyen sáv): egyenletes, halvány (emptyColor) csík.
//
#include "TrackActivityModel.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TrackActivityItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(tanara_qml::TrackActivityModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    // Az idővonal hossza (ms); <= 0 → a modell durationMs-e.
    Q_PROPERTY(int durationMs READ durationMs WRITE setDurationMs NOTIFY durationMsChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor quietColor READ quietColor WRITE setQuietColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor emptyColor READ emptyColor WRITE setEmptyColor NOTIFY colorsChanged)
    // Van-e rajzolható adat ehhez a sávhoz (a QML ezzel vált a „számol" állapotra).
    Q_PROPERTY(bool hasData READ hasData NOTIFY hasDataChanged)

public:
    explicit TrackActivityItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    TrackActivityModel* model() const { return m_model; }
    void setModel(TrackActivityModel* model);
    QString trackId() const { return m_trackId; }
    void setTrackId(const QString& id);
    int durationMs() const { return m_durationMs; }
    void setDurationMs(int ms);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);
    QColor quietColor() const { return m_quietColor; }
    void setQuietColor(const QColor& c);
    QColor emptyColor() const { return m_emptyColor; }
    void setEmptyColor(const QColor& c);
    bool hasData() const;

    // Oszloponként: -1 = nincs adat, különben a beszédes keretek aránya 0..1 (0 = csak csend).
    // A rajz és a teszt közös számítása.
    static QVector<qreal> columns(const QVector<qint8>& frames, qint64 originMs, int frameMs,
                                  qint64 durationMs, int count);

signals:
    void modelChanged();
    void trackIdChanged();
    void durationMsChanged();
    void colorsChanged();
    void hasDataChanged();

private:
    void invalidate();

    QPointer<TrackActivityModel> m_model;
    QMetaObject::Connection m_modelConn;
    QString m_trackId;
    int m_durationMs = 0;
    QColor m_color{Qt::darkCyan};
    QColor m_quietColor{Qt::gray};
    QColor m_emptyColor{Qt::lightGray};
    bool m_lastHasData = false;

    // Gyorsítótár: az utoljára számolt oszlopok (szélesség + modell-revízió szerint).
    QVector<qreal> m_cols;
    int m_cacheWidth = -1;
    int m_cacheRevision = -1;
};

} // namespace tanara_qml
