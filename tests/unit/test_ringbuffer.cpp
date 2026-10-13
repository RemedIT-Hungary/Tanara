#include "tanara/recording/audio/RingBuffer.h"

#include <QtTest/QtTest>

#include <cstdint>
#include <vector>

using tanara::recording::RingBuffer;

class TestRingBuffer : public QObject {
    Q_OBJECT
private slots:
    // Basic FIFO: we write N samples, read them back, order and value match.
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

    // available()'s accuracy after a partial read.
    void partialReadAvailable() {
        RingBuffer rb(256);
        std::vector<int16_t> in(200);
        for (int i = 0; i < 200; ++i) in[i] = static_cast<int16_t>(i);
        QCOMPARE(rb.write(in.data(), in.size()), size_t(200));

        std::vector<int16_t> out(80, 0);
        QCOMPARE(rb.read(out.data(), 80), size_t(80));
        QCOMPARE(rb.available(), size_t(120));
        for (int i = 0; i < 80; ++i) QCOMPARE(out[static_cast<size_t>(i)], int16_t(i));

        // The remaining 120 start at 80.
        std::vector<int16_t> rest(120, 0);
        QCOMPARE(rb.read(rest.data(), 200), size_t(120));  // only 120 exist
        QCOMPARE(rb.available(), size_t(0));
        for (int i = 0; i < 120; ++i) QCOMPARE(rest[static_cast<size_t>(i)], int16_t(80 + i));
    }

    // Wrap-around: we fill / drain so that the physical index wraps.
    void wrapAround() {
        RingBuffer rb(8);                 // capacity 8: at most 7 samples at once
        QCOMPARE(rb.capacity(), size_t(8));

        std::vector<int16_t> a{1, 2, 3, 4, 5};
        QCOMPARE(rb.write(a.data(), a.size()), size_t(5));

        std::vector<int16_t> tmp(3, 0);
        QCOMPARE(rb.read(tmp.data(), 3), size_t(3));   // take out 3 (1,2,3)
        QCOMPARE(tmp[0], int16_t(1));
        QCOMPARE(tmp[2], int16_t(3));
        QCOMPARE(rb.available(), size_t(2));           // 4,5 remain

        // Now we write 5: the physical write index wraps at the end.
        std::vector<int16_t> b{6, 7, 8, 9, 10};
        QCOMPARE(rb.write(b.data(), b.size()), size_t(5));
        QCOMPARE(rb.available(), size_t(7));           // 4,5,6,7,8,9,10

        std::vector<int16_t> out(7, 0);
        QCOMPARE(rb.read(out.data(), 7), size_t(7));
        const int16_t expected[7] = {4, 5, 6, 7, 8, 9, 10};
        for (int i = 0; i < 7; ++i) QCOMPARE(out[static_cast<size_t>(i)], expected[i]);
    }

    // Overflow: a write beyond capacity-1 drops the rest.
    void overflowDrops() {
        RingBuffer rb(16);                              // at most 15 fit
        std::vector<int16_t> in(20);
        for (int i = 0; i < 20; ++i) in[i] = static_cast<int16_t>(i);
        QCOMPARE(rb.write(in.data(), in.size()), size_t(15));
        QCOMPARE(rb.available(), size_t(15));

        // A further write already gives 0 (full).
        int16_t one = 99;
        QCOMPARE(rb.write(&one, 1), size_t(0));
    }

    // An empty read gives 0, does not fail.
    void readEmpty() {
        RingBuffer rb(32);
        std::vector<int16_t> out(10, 0);
        QCOMPARE(rb.read(out.data(), 10), size_t(0));
        QCOMPARE(rb.available(), size_t(0));
    }

    // A non-power-of-two request rounds up.
    void capacityRoundsUp() {
        RingBuffer rb(1000);
        QCOMPARE(rb.capacity(), size_t(1024));
    }
};

QTEST_GUILESS_MAIN(TestRingBuffer)
#include "test_ringbuffer.moc"
