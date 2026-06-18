#include "PeopleManagerDialog.h"

#include "tanara/AppController.h"

#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QStringList>

namespace tanara_gui {

PeopleManagerDialog::PeopleManagerDialog(tanara::AppController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Személyek"));
    resize(360, 420);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        QStringLiteral("Ismert személynevek (minden meetingre érvényes):"), this));

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout();
    m_renameBtn = new QPushButton(QStringLiteral("Átnevezés"), this);
    m_deleteBtn = new QPushButton(QStringLiteral("Törlés"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("Bezárás"), this);
    btnRow->addWidget(m_renameBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_renameBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onRenameClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onDeleteClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &PeopleManagerDialog::updateButtonState);

    // A lista a személyek bármilyen változására frissül (rename/delete → backend jel).
    if (m_controller)
        connect(m_controller, &tanara::AppController::peopleChanged,
                this, &PeopleManagerDialog::refreshList);

    refreshList();
}

QString PeopleManagerDialog::selectedName() const {
    auto* item = m_list->currentItem();
    return item ? item->text() : QString();
}

void PeopleManagerDialog::refreshList() {
    const QString keep = selectedName();
    m_list->clear();
    if (m_controller)
        m_list->addItems(m_controller->knownPeople());
    // Kijelölés visszaállítása, ha még létezik.
    if (!keep.isEmpty()) {
        const auto matches = m_list->findItems(keep, Qt::MatchExactly);
        if (!matches.isEmpty())
            m_list->setCurrentItem(matches.first());
    }
    updateButtonState();
}

void PeopleManagerDialog::updateButtonState() {
    const bool hasSel = !selectedName().isEmpty();
    m_renameBtn->setEnabled(hasSel);
    m_deleteBtn->setEnabled(hasSel);
}

void PeopleManagerDialog::onRenameClicked() {
    if (!m_controller)
        return;
    const QString oldName = selectedName();
    if (oldName.isEmpty())
        return;

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, QStringLiteral("Átnevezés"),
        QStringLiteral("Új név:"), QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    m_controller->renamePerson(oldName, newName);
    // A lista a peopleChanged() jelre frissül.
}

void PeopleManagerDialog::onDeleteClicked() {
    if (!m_controller)
        return;
    const QString name = selectedName();
    if (name.isEmpty())
        return;

    const auto ans = QMessageBox::question(
        this, QStringLiteral("Törlés"),
        QStringLiteral("Biztosan törlöd a(z) „%1” személyt minden meetingből?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    m_controller->removePerson(name);
    // A lista a peopleChanged() jelre frissül.
}

} // namespace tanara_gui
