#pragma once
//
// PipelineStepModel: the Overview's PROCESSING steps as a stable list model, keyed by the step `key`.
//
// OverviewViewModel feeds it the snapshot's step rows on every reload (setSteps). Rows whose key stays
// are updated IN PLACE (dataChanged with only the roles that changed); rows are inserted, removed or
// moved only when the step set or its order changes; the model is never reset. So the Repeater in
// PipelineSteps.qml keeps its delegates (and the panels loaded into them: TranscribeStepPanel, the
// summary controls) across progress ticks and state changes: an open route menu stays anchored, and a
// click handler's object is never destroyed while the handler runs.
//
// Roles (the QML field names of a step row; a missing field reads as the role's default):
//   key, title, sub, state (done | attention | running | waiting), parallel (false),
//   progress (0..100, -1 = no bar (default), -2 = indeterminate), fixTarget, note, detail,
//   phase, canStart (false), route, routeOptions ([]),
//   step: the whole row as a map (every field, also the ones without a role).
//
#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class PipelineStepModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PipelineStepModel is owned by OverviewViewModel (stepModel).")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        SubRole,
        StateRole,
        ParallelRole,
        ProgressRole,
        FixTargetRole,
        NoteRole,
        DetailRole,
        PhaseRole,
        CanStartRole,
        RouteRole,
        RouteOptionsRole,
        StepRole,
    };
    Q_ENUM(Role)

    explicit PipelineStepModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    // The rows as maps, in order (OverviewViewModel::steps).
    QVariantList rows() const;
    // Applies a new step list: keyed in-place update, then insert / remove / move as needed.
    void setSteps(const QVariantList& steps);
    // The row of the step `key` (an empty map if there is none).
    Q_INVOKABLE QVariantMap get(const QString& key) const;
    // The row index of the step `key`, -1 if there is none.
    Q_INVOKABLE int indexOf(const QString& key) const;

signals:
    void countChanged();

private:
    static QString keyOf(const QVariantMap& row);
    static QVariant roleValue(const QVariantMap& row, int role);
    QVector<QVariantMap> m_rows;
};

} // namespace tanara_qml
