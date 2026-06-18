#include "PeopleManagerDialog.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/VoiceprintStore.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
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

    // Jobb oldal: „Mely meetingeken” + „Hang-lenyomatok” panel.
    auto* rightCol = new QVBoxLayout();
    rightCol->addWidget(new QLabel(QStringLiteral("Mely meetingeken:"), this));
    m_meetingsList = new QListWidget(this);
    m_meetingsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_meetingsList->setFocusPolicy(Qt::NoFocus);
    rightCol->addWidget(m_meetingsList, 1);

    m_voiceprintLabel = new QLabel(QStringLiteral("Hang-lenyomatok:"), this);
    rightCol->addWidget(m_voiceprintLabel);
    m_voiceprintList = new QListWidget(this);
    m_voiceprintList->setSelectionMode(QAbstractItemView::SingleSelection);
    rightCol->addWidget(m_voiceprintList, 1);
    m_removePrintBtn = new QPushButton(QStringLiteral("Kijelölt lenyomat törlése"), this);
    rightCol->addWidget(m_removePrintBtn, 0, Qt::AlignLeft);
    mid->addLayout(rightCol, 1);

    root->addLayout(mid, 1);

    auto* btnRow = new QHBoxLayout();
    m_editBtn   = new QPushButton(QStringLiteral("✎ Átnevezés"), this);
    m_deleteBtn = new QPushButton(QStringLiteral("Törlés"), this);
    m_mergeBtn  = new QPushButton(QStringLiteral("Összevonás…"), this);
    m_mergeBtn->setToolTip(QStringLiteral(
        "A kijelölt személy összevonása egy másikkal (a hang-lenyomatok és a "
        "meeting-címkézések átkerülnek)."));
    auto* closeBtn = new QPushButton(QStringLiteral("Bezárás"), this);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addWidget(m_mergeBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_editBtn, &QPushButton::clicked, this, &PeopleManagerDialog::beginEditCurrent);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onDeleteClicked);
    connect(m_mergeBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onMergeClicked);
    connect(m_removePrintBtn, &QPushButton::clicked, this, &PeopleManagerDialog::onRemovePrintClicked);
    connect(m_voiceprintList, &QListWidget::itemSelectionChanged,
            this, &PeopleManagerDialog::updateButtonState);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &PeopleManagerDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemChanged,
            this, &PeopleManagerDialog::onItemChanged);
    // Dupla-kattintás a soron is indítsa az átnevezést (a ✎ mellett kényelmi út).
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { beginEditCurrent(); });

    // A lista a személyek bármilyen változására frissül (rename/delete → backend jel).
    if (m_controller) {
        connect(m_controller, &tanara::AppController::peopleChanged,
                this, &PeopleManagerDialog::refreshList);
        connect(m_controller, &tanara::AppController::voiceprintsChanged,
                this, &PeopleManagerDialog::refreshVoiceprintPanel);
    }

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
    refreshVoiceprintPanel();
}

void PeopleManagerDialog::onSelectionChanged() {
    updateButtonState();
    refreshMeetingsPanel();
    refreshVoiceprintPanel();
}

void PeopleManagerDialog::refreshVoiceprintPanel() {
    if (!m_voiceprintList)
        return;
    m_voiceprintList->clear();
    auto* vp = m_controller ? m_controller->voiceprints() : nullptr;
    const QString name = selectedName();
    if (!vp || name.isEmpty()) {
        m_voiceprintLabel->setText(QStringLiteral("Hang-lenyomatok:"));
        updateButtonState();
        return;
    }
    const QVector<tanara::Voiceprint> prints = vp->printsFor(name);
    m_voiceprintLabel->setText(QStringLiteral("Hang-lenyomatok (%1):").arg(prints.size()));
    for (const tanara::Voiceprint& p : prints) {
        const QString dev = p.device.isEmpty() ? QStringLiteral("ismeretlen eszköz") : p.device;
        const QString when = p.createdAt.left(10);   // yyyy-MM-dd
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(dev, when), m_voiceprintList);
        item->setData(Qt::UserRole, p.id);            // a törléshez
    }
    updateButtonState();
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
    // Összevonás: kell legalább 2 személy, és a kijelölt ne a saját (én) sor legyen.
    if (m_mergeBtn)
        m_mergeBtn->setEnabled(hasSel && !isOwnRow(m_list->currentItem()) && m_list->count() > 1);
    if (m_removePrintBtn)
        m_removePrintBtn->setEnabled(m_voiceprintList
                                     && m_voiceprintList->currentItem() != nullptr);
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

void PeopleManagerDialog::onMergeClicked() {
    if (!m_controller)
        return;
    auto* item = m_list->currentItem();
    if (!item || isOwnRow(item))
        return;
    const QString from = item->data(RoleOriginalName).toString();
    if (from.isEmpty())
        return;

    // A lehetséges célok: minden MÁS sor (a saját (én) sor is lehet cél).
    QStringList targets;
    for (int i = 0; i < m_list->count(); ++i) {
        const QString n = m_list->item(i)->data(RoleOriginalName).toString();
        if (!n.isEmpty() && n != from)
            targets << n;
    }
    if (targets.isEmpty())
        return;

    bool ok = false;
    const QString into = QInputDialog::getItem(
        this, QStringLiteral("Összevonás"),
        QStringLiteral("„%1” összevonása ezzel a személlyel:").arg(from),
        targets, 0, false, &ok);
    if (!ok || into.isEmpty() || into == from)
        return;

    const auto ans = QMessageBox::question(
        this, QStringLiteral("Összevonás megerősítése"),
        QStringLiteral("Biztosan összevonod: „%1” → „%2”?\n"
                       "A(z) „%1” hang-lenyomatai és minden meeting-címkézése átkerül "
                       "„%2” alá, és „%1” megszűnik.").arg(from, into),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    // A renamePerson cél-egyesítő: ha a cél már létezik, a lenyomatok + speakerMap-ek
    // átkerülnek (VoiceprintStore::renamePerson + PeopleStore::rename egyesít).
    m_controller->renamePerson(from, into);
}

void PeopleManagerDialog::onRemovePrintClicked() {
    if (!m_controller || !m_controller->voiceprints() || !m_voiceprintList)
        return;
    auto* item = m_voiceprintList->currentItem();
    if (!item)
        return;
    const QString printId = item->data(Qt::UserRole).toString();
    if (printId.isEmpty())
        return;

    const auto ans = QMessageBox::question(
        this, QStringLiteral("Lenyomat törlése"),
        QStringLiteral("Törlöd ezt a hang-lenyomatot? (A személy és a címkézés megmarad.)"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    m_controller->voiceprints()->removePrint(printId);
    refreshVoiceprintPanel();
}

} // namespace tanara_gui
