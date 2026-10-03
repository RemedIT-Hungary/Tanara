#pragma once
//
// SettingsDeviceModel — a Beállítások „Rögzítés” lapjának (B02) eszközlistája: ugyanaz a
// három csoport és ugyanazok a sorok, mint a felvevőben (kapcsoló, barátságos + nyers név,
// „alapért.”, 14 szegmenses szintmérő csúcstartással), plusz az eszköz átnevezése.
//
// A kapcsolók az „alapértelmezett források” piszkozatát állítják (a felvevővel közös
// kijelölés, state.json), a nevek a piszkozat `deviceNames` tábláját — mindkettő a
// SettingsViewModel mentésekor érvényesül. A szint a core szintfigyeléséből jön
// (AppController::deviceLevelPeak), a skála a felvevőé (RecorderViewModel::levelSegments).
//
#include "tanara/Types.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SettingsViewModel;

class SettingsDeviceModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,   // a nyers OS-eszköznév (stabil kulcs)
        NameRole,                     // megjelenített név (a felhasználóé vagy a rövidített)
        RawNameRole,
        DefaultNameRole,              // a rövidített gyári név (az átnevezés visszavonásához)
        RenamedRole,                  // a felhasználó adott neki nevet
        GroupRole,                    // 0 mikrofon · 1 hangkimenet · 2 egyéb bemenet
        GroupFirstRole,
        SelectedRole,                 // alapértelmezett forrás
        DefaultRole,                  // a rendszer alapértelmezett eszköze
        LevelRole,                    // 0..14 szegmens
        PeakRole,                     // csúcstartás szegmens-indexe (-1 = nincs)
    };

    explicit SettingsDeviceModel(SettingsViewModel* vm);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Az eszközök újraolvasása a core-ból (vagy a demó-lista), a piszkozat neveivel.
    void rebuild();
    // A nevek / kijelölés a piszkozatból változott (eldobás, külső mentés).
    void refreshFromDraft();
    void toggle(int row);
    void rename(int row, const QString& name);
    void onLevel(const QString& deviceName, float rms, float peak);
    void setTicking(bool on);
    // A jelen lévő eszközök nyers nevei (a mentés ezekre írja felül a kijelölést).
    QStringList presentNames() const;

private:
    struct Row {
        tanara::AudioDeviceInfo info;
        int group = 0;
        int level = 0;
        int peakSeg = -1;
        qint64 peakAt = 0;
        bool demoFixed = false;
    };
    void tick();
    void loadDemo();
    int rowOf(const QString& name) const;

    SettingsViewModel* m_vm;
    QVector<Row> m_rows;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

} // namespace tanara_qml
