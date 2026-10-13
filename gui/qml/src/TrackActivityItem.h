#pragma once
//
// Tanara QML: the speech strip's drawing (TrackActivityStrip): one track's timeline over the
// meeting's whole length, per pixel column: no data -> empty; silence -> thin baseline;
// speech -> full-height bar (filled in proportion to the column's speech frames). It
// computes in C++ from the TrackActivityModel's frames (no QML array) and caches the columns
// until the width / data changes, so even a 2 h x 50 ms frame series is fast. The playhead
// and mouse handling are in QML (no repaint on movement).
//   TrackActivityItem { model: activity; trackId: "mic"; color: Theme.speakerLine(0) }
// No data (computing / no such track): an even, dim (emptyColor) stripe.
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
    // The timeline's length (ms); <= 0 -> the model's durationMs.
    Q_PROPERTY(int durationMs READ durationMs WRITE setDurationMs NOTIFY durationMsChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor quietColor READ quietColor WRITE setQuietColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor emptyColor READ emptyColor WRITE setEmptyColor NOTIFY colorsChanged)
    // Whether there is drawable data for this track (QML switches to the "computing" state with it).
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

    // Per column: -1 = no data, otherwise the share of speech frames 0..1 (0 = silence only).
    // Shared computation of the drawing and the test.
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

    // Cache: the columns last computed (by width + model revision).
    QVector<qreal> m_cols;
    int m_cacheWidth = -1;
    int m_cacheRevision = -1;
};

} // namespace tanara_qml
