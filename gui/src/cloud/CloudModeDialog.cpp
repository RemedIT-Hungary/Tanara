#include "cloud/CloudModeDialog.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace tanara_gui {

CloudModeDialog::CloudModeDialog(tanara::AppController*, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Hogyan dolgozzuk fel a meetingjeidet?"));
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("A felvétel és a beszélőfelismerés mindig a gépeden fut. Az átíráshoz és az "
                                "összefoglalóhoz válassz feldolgozót — később a Beállításokban módosíthatod."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto card = [this](const QString& title, const QString& text, const QString& button, Choice c) {
        auto* f = new QFrame(this);
        f->setFrameShape(QFrame::StyledPanel);
        f->setMinimumWidth(260);
        auto* v = new QVBoxLayout(f);
        auto* t = new QLabel(title, f);
        t->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; font-size: 15px; }"));
        v->addWidget(t);
        auto* b = new QLabel(text, f);
        b->setWordWrap(true);
        v->addWidget(b, 1);
        auto* btn = new QPushButton(button, f);
        connect(btn, &QPushButton::clicked, this, [this, c]() { m_choice = c; accept(); });
        v->addWidget(btn);
        return f;
    };
    auto* row = new QHBoxLayout();
    row->addWidget(card(tr("Saját kulcsok (BYO)"),
                        tr("Ingyenes. A saját Soniox-kulcsodat és helyi vagy saját LLM-edet használod. "
                           "A hang közvetlenül a te szolgáltatódhoz megy, vagy teljesen helyben marad."),
                        tr("Saját kulcsok beállítása"), Byo));
    row->addWidget(card(tr("Tanara Cloud"),
                        tr("Regisztráció után azonnal működik, havidíj nélkül. Próbaegyenleget adunk "
                           "kártya-ellenőrzés után. A hang és az átirat a szerverünkön át a szolgáltatóhoz "
                           "megy. Nem tároljuk: a feldolgozás után töröljük."),
                        tr("Tanara Cloud — bejelentkezés"), Cloud));
    lay->addLayout(row);

    auto* box = new QDialogButtonBox(this);
    box->addButton(tr("Később"), QDialogButtonBox::RejectRole);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
}

} // namespace tanara_gui
