#pragma once
//
// Tanara — határidővel megvárt háttér-munka (a hangeszköz-lebontás biztonsági hálója).
//
// A munka azonnal elindul egy saját, leválasztott (detached) szálon. A hívó legfeljebb a
// határidőig vár rá (waitUntil). Ha a munka addig nem végez, a hívó tovább megy, a szál pedig
// magától fut tovább, és a végén felszabadítja, amit a munka birtokol. Így egy beragadt
// rendszerhívás (pl. egy eltűnt eszköz miniaudio-szála) nem fagyaszthatja be a hívó szálat
// (a GUI-t); a legrosszabb eset egy elszivárgott, beragadt szál.
//
// A munka nem hivatkozhat a hívó élettartamához kötött objektumra: mindent érték szerint vagy
// saját tulajdonként kell átvennie, mert a hívó a határidő után már nem vár rá.
//
#include <chrono>
#include <functional>
#include <memory>

namespace tanara {

class DetachedJob {
public:
    using Clock = std::chrono::steady_clock;

    // Elindítja a munkát egy új, leválasztott szálon.
    explicit DetachedJob(std::function<void()> work);
    ~DetachedJob();

    DetachedJob(DetachedJob&&) noexcept;
    DetachedJob& operator=(DetachedJob&&) noexcept;
    DetachedJob(const DetachedJob&)            = delete;
    DetachedJob& operator=(const DetachedJob&) = delete;

    // Vár a munka végéig, legfeljebb a határidőig. Igaz: a munka befejeződött.
    bool waitUntil(Clock::time_point deadline) const;
    bool waitFor(std::chrono::milliseconds timeout) const { return waitUntil(Clock::now() + timeout); }
    bool finished() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace tanara
