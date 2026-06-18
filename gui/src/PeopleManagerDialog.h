#pragma once
//
// PeopleManagerDialog — ismert személynevek (globális) kezelése.
//   QListWidget a controller->knownPeople() listával, soronkénti inline
//   szerkesztés egy ceruza (✎) gombbal + Törlés. A rename/delete a controller
//   renamePerson/removePerson hívásokat indítja (globális, minden meetingre kihat),
//   a lista a peopleChanged() jelre frissül.
//
//   A legfelső sor a saját (mic-sáv) beszélőnév „(én)” jelöléssel — ennek
//   szerkesztése setUserSpeakerName-et hív (NEM renamePerson).
//
//   Egy sor kijelölésekor a jobb/alsó panel megmutatja, mely meetingeken
//   szerepelt az adott személy (meetingsForPerson) — az azonosítást segíti.
//
// Csak gui/-ben él, namespace tanara_gui.
//
#include <QDialog>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class PeopleManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit PeopleManagerDialog(tanara::AppController* controller, QWidget* parent = nullptr);

private slots:
    void onDeleteClicked();
    void refreshList();
    void updateButtonState();
    void beginEditCurrent();          // a kijelölt sor inline szerkesztésbe vált
    void onItemChanged(QListWidgetItem* item);   // szerkesztés commitja
    void onSelectionChanged();

private:
    QString selectedName() const;
    bool isOwnRow(QListWidgetItem* item) const;
    void refreshMeetingsPanel();

    tanara::AppController* m_controller = nullptr;
    QListWidget* m_list = nullptr;
    QListWidget* m_meetingsList = nullptr;
    QPushButton* m_editBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    bool m_refreshing = false;   // a refresh alatti itemChanged-jeleket nyeljük el
    QString m_ownName;           // a saját beszélőnév (én) az aktuális frissítéskor
};

} // namespace tanara_gui
