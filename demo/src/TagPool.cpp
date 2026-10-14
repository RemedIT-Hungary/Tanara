#include "demo/TagPool.h"

#include "demo/DemoText.h"

namespace tanara_demo {

QString PoolTag::name() const { return demoText(hu, en); }

namespace tagpool {

const std::vector<PoolTag>& all()
{
    static const std::vector<PoolTag> pool{
        {"t-nordvik", "Nordvik", "Nordvik"},
        {"t-q4", "Q4 tervezés", "Q4 planning"},
        {"t-termek", "Termék", "Product"},
        {"t-partnerek", "Partnerek", "Partners"},
        {"t-belso", "Belső", "Internal"},
        {"t-muzeo", "Muzeo", "Muzeo"},
        {"t-mem", "MÉM-MDK", "Heritage Lab"},
        {"t-gyujtemeny", "Gyűjteménykezelés", "Collections"},
        {"t-ugyfel", "Ügyféltámogatás", "Customer support"},
        {"t-palyazat", "Múzeumi pályázat", "Museum grant"},
        {"t-nordvikas", "Nordvik AS", "Nordvik AS"},
        {"t-kutatas", "Kutatás", "Research"},
    };
    return pool;
}

const PoolTag* byId(const QString& id)
{
    for (const PoolTag& t : all())
        if (id == QLatin1String(t.id)) return &t;
    return nullptr;
}

QString name(const char* id)
{
    const PoolTag* t = byId(QString::fromLatin1(id));
    return t ? t->name() : QString();
}

} // namespace tagpool

} // namespace tanara_demo
