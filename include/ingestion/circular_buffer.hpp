#ifndef OCAS_INGESTION_CIRCULAR_BUFFER_HPP
#define OCAS_INGESTION_CIRCULAR_BUFFER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

inline constexpr std::size_t kCacheLineBytes = 64;

enum class PushResult : std::uint8_t { Stored, OverwroteOldest };

// Fixed ring buffer. When full, push overwrites the oldest element and counts it.
template <typename T, std::size_t Capacity>
class alignas(kCacheLineBytes) CircularBuffer {
    static_assert(Capacity > 0);

public:
    PushResult push(const T& item) noexcept {
        if (count_ < Capacity) {
            storage_[wrap(head_ + count_)] = item;
            ++count_;
            return PushResult::Stored;
        }
        storage_[head_] = item;
        head_ = wrap(head_ + 1);
        ++overflow_count_;
        return PushResult::OverwroteOldest;
    }

    bool pop(T& out) noexcept {
        if (count_ == 0) return false;
        out = storage_[head_];
        head_ = wrap(head_ + 1);
        --count_;
        return true;
    }

    // 0 is the oldest. Caller checks i < size().
    [[nodiscard]] const T& at(std::size_t i) const noexcept { return storage_[wrap(head_ + i)]; }

    template <typename Predicate>
    [[nodiscard]] bool any_of(Predicate&& pred) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (pred(at(i))) return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::uint64_t overflow_count() const noexcept { return overflow_count_; }

private:
    static constexpr std::size_t wrap(std::size_t i) noexcept { return i % Capacity; }

    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::uint64_t overflow_count_ = 0;
    std::array<T, Capacity> storage_{};
};

}  // namespace ocas

#endif
