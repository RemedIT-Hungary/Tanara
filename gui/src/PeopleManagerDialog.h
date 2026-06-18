#pragma once
//
// PeopleManagerDialog — ismert személynevek (globális) kezelése.
//   QListWidget a controller->knownPeople() listával + Átnevezés / Törlés gombok.
//   A rename/delete a controller renamePerson/removePerson hívásokat indítja
//   (globális, minden meetingre kihat), a lista a peopleChanged() jelre frissül.
//
// Csak gui/-ben él, namespace tanara_gui.
//
#include <QDialog>

class QListWidget;
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
    void onRenameClicked();
    void onDeleteClicked();
    void refreshList();
    void updateButtonState();

private:
    QString selectedName() const;

    tanara::AppController* m_controller = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_renameBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
};

} // namespace tanara_gui
