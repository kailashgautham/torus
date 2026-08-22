#ifndef TORUS_LOCKFREE_RING_BUFFER_H
#define TORUS_LOCKFREE_RING_BUFFER_H

#include <atomic>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <vector>

template <typename T> class lockfree_ring_buffer
{
  public:
    explicit lockfree_ring_buffer(const uint32_t capacity)
        : capacity_{std::bit_ceil(capacity)}, ring_buffer_(capacity_)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("capacity must be non-zero");
        }
        if (capacity != capacity_)
        {
            std::fprintf(stderr, "torus: capacity %u rounded up to %u (must be a power of two)\n",
                         capacity, capacity_);
        }
    }

    bool try_push(const T& item)
    {
        uint32_t current_write_idx = write_idx_.load(std::memory_order_relaxed);
        uint32_t current_read_idx = read_idx_.load(std::memory_order_acquire);

        if (current_write_idx - current_read_idx >= capacity_)
        {
            // ring buffer is full
            return false;
        }

        ring_buffer_[current_write_idx & (capacity_ - 1)] = item;

        write_idx_.store(current_write_idx + 1, std::memory_order_release);

        return true;
    }

    bool try_pop(T& item)
    {
        uint32_t current_read_idx = read_idx_.load(std::memory_order_relaxed);
        uint32_t current_write_idx = write_idx_.load(std::memory_order_acquire);

        if (current_read_idx == current_write_idx)
        {
            // ring buffer is empty
            return false;
        }

        item = std::move(ring_buffer_[current_read_idx & (capacity_ - 1)]);

        read_idx_.store(current_read_idx + 1, std::memory_order_release);

        return true;
    }

  private:
    alignas(64) std::atomic<uint32_t> read_idx_{0};
    alignas(64) std::atomic<uint32_t> write_idx_{0};
    uint32_t capacity_;
    std::vector<T> ring_buffer_;
};

#endif // TORUS_LOCKFREE_RING_BUFFER_H
