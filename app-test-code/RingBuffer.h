#ifndef __RINGBUFFER_H_
#define __RINGBUFFER_H_

#include <stddef.h>

template <typename T>
class RingBuffer {
 public:
    RingBuffer() { Reset(1024); }

    explicit RingBuffer(size_t capacity) { Reset(capacity); }

    RingBuffer(const RingBuffer& other) = default;
    RingBuffer(RingBuffer&& other) noexcept = default;
    RingBuffer& operator=(const RingBuffer& other) = default;
    RingBuffer& operator=(RingBuffer&& other) noexcept = default;

    void Append(const T& val) {
        if (IsFull())
        PopFront();
        Get(size_) = val;
        size_++;
    }

    void Append(T&& val) {
        if (IsFull())
        PopFront();
        Get(size_) = std::move(val);
        size_++;
    }

    bool IsEmpty() const { return size_ == 0; }

    bool IsFull() const { return size_ == buffer_.size(); }

    size_t GetSize() const { return size_; }

    size_t GetCapacity() const { return buffer_.size(); }

    T& Get(size_t i) { return buffer_[(start_ + i) % buffer_.size()]; }

    const T& Get(size_t i) const {
        return buffer_[(start_ + i) % buffer_.size()];
    }

    const T& Back() const { return Get(size_ - 1); }

    T& Back() { return Get(size_ - 1); }

    const T& Front() const { return Get(0); }

    T& Front() { return Get(0); }

    void PopBack() {
        if (size_ != 0) {
        Get(size_ - 1) = T();
        size_--;
        }
    }

    void PopFront() {
        if (size_ != 0) {
        Get(0) = T();
        start_ = (start_ + 1) % buffer_.size();
        size_--;
        }
    }

    void Clear() { Reset(GetCapacity()); }

    void Reset(size_t capacity) {
        start_ = size_ = 0;
        buffer_.clear();
        buffer_.resize(capacity);
    }

 private:
  // Ideally we'd allocate our own memory and use placement new to instantiate
  // instances of T instead of using a vector, but the vector is simpler.
  std::vector<T> buffer_;
  size_t start_, size_ = 0;
};

#endif