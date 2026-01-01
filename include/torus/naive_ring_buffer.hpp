#ifndef TORUS_NAIVE_RING_BUFFER_H
#define TORUS_NAIVE_RING_BUFFER_H

#include <vector>
#include <condition_variable>
#include <mutex>

template <typename T> class naive_ring_buffer
{
public:
  explicit naive_ring_buffer(const uint32_t capacity) : head_{0}, tail_{0}, size_{0}, capacity_{capacity}, ring_buffer_(capacity) {}

  void push(const T& item)
  {
    std::unique_lock lock(m_);
    cv_.wait(lock, [this]() { return size_ < capacity_; });
    ring_buffer_[tail_] = item;
    tail_ = (tail_ + 1) % capacity_;
    ++size_;
    cv_.notify_one();
  }

  T pop()
  {
    std::unique_lock lock(m_);
    cv_.wait(lock, [this]() { return size_ > 0; });
    T item = std::move(ring_buffer_[head_]);
    head_ = (head_ + 1) % capacity_;
    --size_;
    cv_.notify_one();
    return item;
  }

  bool is_empty()
  {
    std::lock_guard lock(m_);
    return size_ == 0;
  }

  bool is_full()
  {
    std::lock_guard lock(m_);
    return size_ == capacity_;
  }

  uint32_t get_size()
  {
    std::lock_guard lock(m_);
    return size_;
  }

private:
  uint32_t head_;
  uint32_t tail_;
  uint32_t size_;
  uint32_t capacity_;
  std::mutex m_;
  std::condition_variable cv_;
  std::vector<T> ring_buffer_;
};

#endif // TORUS_NAIVE_RING_BUFFER_H