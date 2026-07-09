#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace xray {

template<typename T, size_t Capacity = 4096>
class mpsc_queue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    struct slot {
        std::atomic<T*> data{nullptr};
    };

    std::array<slot, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<size_t> drop_count_{0};

public:
    bool try_enqueue(T* item) {
        size_t pos;
        size_t t;
        size_t h;
        for (;;) {
            h = head_.load(std::memory_order_relaxed);
            t = tail_.load(std::memory_order_acquire);
            if (h - t >= Capacity) {
                drop_count_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            pos = h;
            if (head_.compare_exchange_weak(pos, pos + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
                break;
            }
        }
        buffer_[pos & MASK].data.store(item, std::memory_order_release);
        return true;
    }

    T* try_dequeue() {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t >= head_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        T* item = buffer_[t & MASK].data.load(std::memory_order_acquire);
        if (item == nullptr) {
            return nullptr;
        }
        buffer_[t & MASK].data.store(nullptr, std::memory_order_release);
        tail_.store(t + 1, std::memory_order_release);
        return item;
    }

    size_t drop_count() const {
        return drop_count_.load(std::memory_order_relaxed);
    }

    void reset_drop_count() {
        drop_count_.store(0, std::memory_order_relaxed);
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) <= tail_.load(std::memory_order_acquire);
    }
};

template<typename T, size_t Capacity = 64>
class spsc_queue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    bool try_enqueue(T item) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= Capacity) {
            return false;
        }
        buffer_[h & MASK] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool try_dequeue(T& item) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t >= head_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[t & MASK];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) <= tail_.load(std::memory_order_acquire);
    }
};

template<typename T, size_t Capacity = 4096>
class mpsc_msg_queue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    using storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    std::array<storage, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<uint32_t> drop_count_{0};
    uint32_t warn_threshold_{64};

public:
    bool try_enqueue(T&& item) {
        size_t pos;
        size_t t;
        for (;;) {
            auto h = head_.load(std::memory_order_relaxed);
            t = tail_.load(std::memory_order_acquire);
            if (h - t >= Capacity) {
                drop_count_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            pos = h;
            if (head_.compare_exchange_weak(pos, pos + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
                break;
            }
        }
        new (&buffer_[pos & MASK]) T(std::move(item));
        return true;
    }

    T try_dequeue() {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t >= head_.load(std::memory_order_acquire)) {
            return T{};
        }
        auto& slot = reinterpret_cast<T&>(buffer_[t & MASK]);
        T result = std::move(slot);
        slot.~T();
        tail_.store(t + 1, std::memory_order_release);
        return result;
    }

    uint32_t drop_count() const {
        return drop_count_.load(std::memory_order_relaxed);
    }

    void reset_drop_count() {
        drop_count_.store(0, std::memory_order_relaxed);
    }

    void set_warn_threshold(uint32_t n) {
        warn_threshold_ = n;
    }

    uint32_t warn_threshold() const {
        return warn_threshold_;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) <= tail_.load(std::memory_order_acquire);
    }
};

} // namespace xray
