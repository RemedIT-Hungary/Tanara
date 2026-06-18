#pragma once
//
// Tanara — egy-termelő / egy-fogyasztó (SPSC) lock-free körpuffer int16_t mintákhoz.
//
// Szándékosan Qt-mentes és allokáció-mentes a read/write úton, hogy biztonságosan
// használható legyen valós idejű audio callbackből (termelő) és a lemezre író
// worker szálból (fogyasztó). A kapacitás kettő hatványa, így a maszkolás (& mask)
// helyettesíti a modulót.
//
// Helyesség (SPSC modell):
//   - csak a termelő írja a head_-et, csak a fogyasztó írja a tail_-t;
//   - a head_ release-store / acquire-load párosítja a minta-írásokat az olvasóval,
//     a tail_ ugyanígy fordítva. Egyetlen termelő + egyetlen fogyasztó esetén
//     elég az atomi index-pár, mutex nem kell.
//
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace tanara {

class RingBuffer {
public:
    // requestedCapacity: a kívánt minimális mintaszám. Felfelé kerekül a következő
    // kettő-hatványra. A ténylegesen tárolható mintaszám capacity()-1 (egy slot
    // mindig üresen marad, hogy a "tele" és "üres" állapot megkülönböztethető legyen).
    explicit RingBuffer(size_t requestedCapacity = 1u << 16) {
        size_t cap = nextPow2(requestedCapacity < 2 ? 2 : requestedCapacity);
        capacity_ = cap;
        mask_     = cap - 1;
        data_     = std::make_unique<int16_t[]>(cap);
    }

    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Termelő oldal. Beírja a mintákat (legfeljebb a szabad helyig); a ténylegesen
    // beírt mintaszámot adja vissza. Túlcsordulásnál csendben eldobja a maradékot.
    size_t write(const int16_t* src, size_t count) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        const size_t free = capacity_ - 1 - distance(head, tail);
        if (count > free) count = free;
        if (count == 0) return 0;

        const size_t firstChunk = capacity_ - (head & mask_);
        const size_t n1 = count < firstChunk ? count : firstChunk;
        std::memcpy(&data_[head & mask_], src, n1 * sizeof(int16_t));
        if (count > n1) {
            std::memcpy(&data_[0], src + n1, (count - n1) * sizeof(int16_t));
        }

        head_.store(head + count, std::memory_order_release);
        return count;
    }

    // Fogyasztó oldal. Kiolvas legfeljebb count mintát a dst-be; a ténylegesen
    // kiolvasott mintaszámot adja vissza.
    size_t read(int16_t* dst, size_t count) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        const size_t avail = distance(head, tail);
        if (count > avail) count = avail;
        if (count == 0) return 0;

        const size_t firstChunk = capacity_ - (tail & mask_);
        const size_t n1 = count < firstChunk ? count : firstChunk;
        std::memcpy(dst, &data_[tail & mask_], n1 * sizeof(int16_t));
        if (count > n1) {
            std::memcpy(dst + n1, &data_[0], (count - n1) * sizeof(int16_t));
        }

        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    // Olvasásra kész minták száma. Bármely szálról hívható (közelítő, ha közben
    // a másik oldal dolgozik), de SPSC mellett a saját oldaláról pontos.
    size_t available() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return distance(head, tail);
    }

    // A puffer kapacitása mintában (kettő hatványa). Hasznos slot = capacity()-1.
    size_t capacity() const { return capacity_; }

private:
    // head és tail monoton növő (nem maszkolt) számlálók; a köztük lévő távolság
    // a feltöltöttség. A különbség unsigned-wrap-biztos.
    static size_t distance(size_t head, size_t tail) { return head - tail; }

    static size_t nextPow2(size_t v) {
        if (v && !(v & (v - 1))) return v;  // már kettő hatványa
        size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    std::unique_ptr<int16_t[]> data_;
    size_t capacity_ = 0;
    size_t mask_     = 0;

    // Cache-line-szerű elválasztás a hamis megosztás csökkentésére.
    alignas(64) std::atomic<size_t> head_{0};   // csak a termelő írja
    alignas(64) std::atomic<size_t> tail_{0};   // csak a fogyasztó írja
};

} // namespace tanara
