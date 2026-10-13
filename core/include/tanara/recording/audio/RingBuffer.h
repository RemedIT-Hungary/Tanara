#pragma once
//
// Single-producer / single-consumer (SPSC) lock-free ring buffer for int16_t samples.
//
// Deliberately Qt-free and allocation-free on the read/write path, so it can be used safely from a
// real-time audio callback (producer) and from the worker thread writing to disk (consumer). The
// capacity is a power of two, so masking (& mask) replaces the modulo.
//
// Correctness (SPSC model):
//   - only the producer writes head_, only the consumer writes tail_;
//   - head_'s release-store / acquire-load pairs the sample writes with the reader, and
//     tail_ likewise the other way. With a single producer + a single consumer the atomic
//     index pair is enough, no mutex is needed.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace tanara::recording {

class RingBuffer {
public:
    // requestedCapacity: the desired minimum sample count. Rounded up to the next power of two. The
    // number of samples actually storable is capacity()-1 (one slot always stays empty so "full" and
    // "empty" can be told apart).
    explicit RingBuffer(size_t requestedCapacity = 1u << 16) {
        size_t cap = nextPow2(requestedCapacity < 2 ? 2 : requestedCapacity);
        capacity_ = cap;
        mask_     = cap - 1;
        data_     = std::make_unique<int16_t[]>(cap);
    }

    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Producer side. Writes the samples (up to the free space); returns the number of samples actually
    // written. On overflow it silently drops the rest.
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

    // Consumer side. Reads at most count samples into dst; returns the number of samples actually read.
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

    // Number of samples ready to read. Callable from any thread (approximate if the other side is
    // working meanwhile), but exact from its own side under SPSC.
    size_t available() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return distance(head, tail);
    }

    // Capacity of the buffer in samples (a power of two). Usable slots = capacity()-1.
    size_t capacity() const { return capacity_; }

private:
    // head and tail are monotonically growing (unmasked) counters; the distance between them is the
    // fill level. The difference is unsigned-wrap safe.
    static size_t distance(size_t head, size_t tail) { return head - tail; }

    static size_t nextPow2(size_t v) {
        if (v && !(v & (v - 1))) return v;  // already a power of two
        size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    std::unique_ptr<int16_t[]> data_;
    size_t capacity_ = 0;
    size_t mask_     = 0;

    // Cache-line-like separation to reduce false sharing.
    alignas(64) std::atomic<size_t> head_{0};   // written only by the producer
    alignas(64) std::atomic<size_t> tail_{0};   // written only by the consumer
};

} // namespace tanara::recording