#include "tanara/audio/RingBuffer.h"

#include <QtTest/QtTest>

#include <cstdint>
#include <vector>

using tanara::RingBuffer;

class TestRingBuffer : public QObject {
    Q_OBJECT
private slots:
    // Alap FIFO: beírunk N mintát, visszaolvassuk, sorrend és érték egyezik.
    void fifoOrder() {
        RingBuffer rb(1024);
        std::vector<int16_t> in(500);
        for (int i = 0; i < 500; ++i) in[i] = static_cast<int16_t>(i - 250);

        QCOMPARE(rb.write(in.data(), in.size()), size_t(500));
        QCOMPARE(rb.available(), size_t(500));

        std::vector<int16_t> out(500, 0);
        QCOMPARE(rb.read(out.data(), out.size()), size_t(500));
        QCOMPARE(rb.available(), size_t(0));

        for (int i = 0; i < 500; ++i) {
            QCOMPARE(out[static_cast<size_t>(i)], in[static_cast<size_t>(i)]);
        }
    }

    // available() pontossága részleges olvasás után.
    void partialReadAvailable() {
        RingBuffer rb(256);
        std::vector<int16_t> in(200);
        for (int i = 0; i < 200; ++i) in[i] = static_cast<int16_t>(i);
        QCOMPARE(rb.write(in.data(), in.size()), size_t(200));

        std::vector<int16_t> out(80, 0);
        QCOMPARE(rb.read(out.data(), 80), size_t(80));
        QCOMPARE(rb.available(), size_t(120));
        for (int i = 0; i < 80; ++i) QCOMPARE(out[static_cast<size_t>(i)], int16_t(i));

        // A maradék 120 a 80-tól indul.
        std::vector<int16_t> rest(120, 0);
        QCOMPARE(rb.read(rest.data(), 200), size_t(120));  // csak 120 van
        QCOMPARE(rb.available(), size_t(0));
        for (int i = 0; i < 120; ++i) QCOMPARE(rest[static_cast<size_t>(i)], int16_t(80 + i));
    }

    // Wrap-around: úgy töltünk/ürítünk, hogy a fizikai index átforduljon.
    void wrapAround() {
        RingBuffer rb(8);                 // capacity 8 → max 7 minta egyszerre
        QCOMPARE(rb.capacity(), size_t(8));

        std::vector<int16_t> a{1, 2, 3, 4, 5};
        QCOMPARE(rb.write(a.data(), a.size()), size_t(5));

        std::vector<int16_t> tmp(3, 0);
        QCOMPARE(rb.read(tmp.data(), 3), size_t(3));   // kiveszünk 3-at (1,2,3)
        QCOMPARE(tmp[0], int16_t(1));
        QCOMPARE(tmp[2], int16_t(3));
        QCOMPARE(rb.available(), size_t(2));           // 4,5 maradt

        // Most írunk 5-öt → a fizikai írás-index átfordul a végén.
        std::vector<int16_t> b{6, 7, 8, 9, 10};
        QCOMPARE(rb.write(b.data(), b.size()), size_t(5));
        QCOMPARE(rb.available(), size_t(7));           // 4,5,6,7,8,9,10

        std::vector<int16_t> out(7, 0);
        QCOMPARE(rb.read(out.data(), 7), size_t(7));
        const int16_t expected[7] = {4, 5, 6, 7, 8, 9, 10};
        for (int i = 0; i < 7; ++i) QCOMPARE(out[static_cast<size_t>(i)], expected[i]);
    }

    // Túlcsordulás: a kapacitás-1 fölötti írás eldobja a maradékot.
    void overflowDrops() {
        RingBuffer rb(16);                              // max 15 fér be
        std::vector<int16_t> in(20);
        for (int i = 0; i < 20; ++i) in[i] = static_cast<int16_t>(i);
        QCOMPARE(rb.write(in.data(), in.size()), size_t(15));
        QCOMPARE(rb.available(), size_t(15));

        // További írás már 0-t ad (tele).
        int16_t one = 99;
        QCOMPARE(rb.write(&one, 1), size_t(0));
    }

    // Üres olvasás 0-t ad, nem hibázik.
    void readEmpty() {
        RingBuffer rb(32);
        std::vector<int16_t> out(10, 0);
        QCOMPARE(rb.read(out.data(), 10), size_t(0));
        QCOMPARE(rb.available(), size_t(0));
    }

    // Nem-kettőhatvány kérés felfelé kerekül.
    void capacityRoundsUp() {
        RingBuffer rb(1000);
        QCOMPARE(rb.capacity(), size_t(1024));
    }
};

QTEST_GUILESS_MAIN(TestRingBuffer)
#include "test_ringbuffer.moc"
