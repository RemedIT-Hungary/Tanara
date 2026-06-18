#include "PeopleManagerDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QStringList>
#include <QFont>

namespace tanara_gui {

namespace {
// Az item adatszerepei.
constexpr int RoleOriginalName = Qt::UserRole;       // a szerkesztés előtti név
constexpr int RoleIsOwn        = Qt::UserRole + 1;   // bool: a saját (én) sor-e
} // namespace

PeopleManagerDialog::PeopleManagerDialog(tanara::AppController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {

    setWindowTitle(QStringLiteral("Személyek"));
    resize(560, 420);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        QStringLiteral("Ismert személynevek (minden meetingre érvényes). "
                       "A ceruzával (✎) átnevezhető a kijelölt sor."), this));

    auto* mid = new QHBoxLayout();

    // Bal oldal: a személylista.
    auto* leftCol = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // Szerkesztés csak explicit módon (✎ vagy F2), ne dupla-kattintásra véletlenül.
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    leftCol->addWidget(m_list, 1);
    mid->addLayout(leftCol, 1);

    // Jobb oldal: „Mely meetingeken” panel.
    auto* rightCol = new QVBoxLayout();
    rightCol->addWidget(new QLabel(QStringLiteral("Mely meetingeken:"), this));
    m_meetingsList = new QListWidget(this);
    m_meetingsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_meetingsList->setFocusPolicy(Qt::NoFocus);
    rightCol->addWidget(m_meetingsList, 1);
    mid->addLayout(rightCol, 1);

    root->addLayout(mid, 1);

    auto* btnRow = new QHBoxLayout();
    m_editBtn   = new QPushButton(QStringLiteral("✎ Átnevezés"), this);
    m_deleteBtn = new QPushButton(QStringLiteral("Törlés"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("Bezárás"), this);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_editBtn, &QPushButton::clicked, this, &PeopleManagerDialog::beginEditCurrent);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onDeleteClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &PeopleManagerDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemChanged,
            this, &PeopleManagerDialog::onItemChanged);
    // Dupla-kattintás a soron is indítsa az átnevezést (a ✎ mellett kényelmi út).
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { beginEditCurrent(); });

    // A lista a személyek bármilyen változására frissül (rename/delete → backend jel).
    if (m_controller)
        connect(m_controller, &tanara::AppController::peopleChanged,
                this, &PeopleManagerDialog::refreshList);

    refreshList();
}

bool PeopleManagerDialog::isOwnRow(QListWidgetItem* item) const {
    return item && item->data(RoleIsOwn).toBool();
}

QString PeopleManagerDialog::selectedName() const {
    auto* item = m_list->currentItem();
    if (!item)
        return QString();
    // A saját sor esetén az aktuális (én)-jelölés nélküli nevet adjuk vissza.
    return item->data(RoleOriginalName).toString();
}

void PeopleManagerDialog::refreshList() {
    const QString keep = selectedName();

    m_refreshing = true;
    m_list->clear();

    m_ownName.clear();
    if (m_controller && m_controller->settings())
        m_ownName = m_controller->settings()->settings().userSpeakerName.trimmed();

    // 1) A saját (én) sor legfelülre, ha van neve.
    if (!m_ownName.isEmpty()) {
        auto* own = new QListWidgetItem(
            QStringLiteral("%1  (én)").arg(m_ownName), m_list);
        own->setData(RoleOriginalName, m_ownName);
        own->setData(RoleIsOwn, true);
        own->setFlags(own->flags() | Qt::ItemIsEditable);
        QFont f = own->font();
        f.setBold(true);
        own->setFont(f);
    }

    // 2) A többi ismert személy (a saját nevet ne duplikáljuk).
    if (m_controller) {
        for (const QString& name : m_controller->knownPeople()) {
            if (!m_ownName.isEmpty() && name == m_ownName)
                continue;   // a saját nevet a fenti (én) sor képviseli
            auto* it = new QListWidgetItem(name, m_list);
            it->setData(RoleOriginalName, name);
            it->setData(RoleIsOwn, false);
            it->setFlags(it->flags() | Qt::ItemIsEditable);
        }
    }

    m_refreshing = false;

    // Kijelölés visszaállítása, ha még létezik.
    if (!keep.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(RoleOriginalName).toString() == keep) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }

    updateButtonState();
    refreshMeetingsPanel();
}

void PeopleManagerDialog::onSelectionChanged() {
    updateButtonState();
    refreshMeetingsPanel();
}

void PeopleManagerDialog::refreshMeetingsPanel() {
    m_meetingsList->clear();
    if (!m_controller)
        return;
    const QString name = selectedName();
    if (name.isEmpty())
        return;
    const QStringList meetings = m_controller->meetingsForPerson(name);
    if (meetings.isEmpty()) {
        auto* placeholder = new QListWidgetItem(
            QStringLiteral("(nincs találat)"), m_meetingsList);
        placeholder->setFlags(Qt::NoItemFlags);
    } else {
        m_meetingsList->addItems(meetings);
    }
}

void PeopleManagerDialog::updateButtonState() {
    const bool hasSel = m_list->currentItem() != nullptr;
    m_editBtn->setEnabled(hasSel);
    // A saját (én) sor nem törölhető — csak átnevezhető (setUserSpeakerName).
    m_deleteBtn->setEnabled(hasSel && !isOwnRow(m_list->currentItem()));
}

void PeopleManagerDialog::beginEditCurrent() {
    auto* item = m_list->currentItem();
    if (!item)
        return;
    m_list->editItem(item);   // inline QLineEdit a soron; commit → onItemChanged
}

void PeopleManagerDialog::onItemChanged(QListWidgetItem* item) {
    if (m_refreshing || !m_controller || !item)
        return;

    const QString oldName = item->data(RoleOriginalName).toString();
    const bool own = item->data(RoleIsOwn).toBool();

    // Az inline szerkesztő nyers szövege; a saját sornál levágjuk az „(én)” jelölést,
    // ha a felhasználó nem írta át teljesen.
    QString newName = item->text().trimmed();
    const QString ownSuffix = QStringLiteral("(én)");
    if (own && newName.endsWith(ownSuffix))
        newName = newName.left(newName.size() - ownSuffix.size()).trimmed();

    if (newName.isEmpty() || newName == oldName) {
        // Nincs valódi változás → a megjelenítést visszaállítjuk (refreshList).
        refreshList();
        return;
    }

    if (own)
        m_controller->setUserSpeakerName(newName);   // saját név → beállítás + DB
    else
        m_controller->renamePerson(oldName, newName); // globális átnevezés

    // A lista a peopleChanged() jelre frissül; ha mégsem jönne jel, a refreshList
    // a következő interakcióig a régi szöveget mutatná, ezért itt is kérünk egyet.
    refreshList();
}

void PeopleManagerDialog::onDeleteClicked() {
    if (!m_controller)
        return;
    auto* item = m_list->currentItem();
    if (!item || isOwnRow(item))
        return;
    const QString name = item->data(RoleOriginalName).toString();
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
