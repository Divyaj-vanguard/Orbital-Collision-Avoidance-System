// ============================================================================
// OCAS Module 1: Telemetry Ingestion Buffer
// CircularBuffer<T, Capacity>: fixed-array ring buffer, overwrite-oldest policy.
// ============================================================================
// Guarantees
//   * Storage is a std::array member: no heap allocation, ever.
//   * push / pop / peek are O(1) with no loops and no branches on capacity.
//   * When full, push() overwrites the oldest element, reports it through the
//     return value, and increments a lifetime overflow counter. Data loss is
//     therefore never silent.
//   * The object is aligned to a 64-byte cache line so that the hot indices
//     and the first elements do not share a line with unrelated data.
// ============================================================================
#ifndef OCAS_INGESTION_CIRCULAR_BUFFER_HPP
#define OCAS_INGESTION_CIRCULAR_BUFFER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ocas {

inline constexpr std::size_t kCacheLineBytes = 64;

enum class PushResult : std::uint8_t {
    Stored,          ///< Element stored in a free slot
    OverwroteOldest  ///< Buffer was full; oldest element was evicted
};

template <typename T, std::size_t Capacity>
class alignas(kCacheLineBytes) CircularBuffer {
    static_assert(Capacity > 0, "CircularBuffer capacity must be non-zero");
    static_assert(std::is_default_constructible_v<T>, "T must be default constructible");
    static_assert(std::is_nothrow_copy_assignable_v<T>, "T must be nothrow copy assignable");

public:
    using value_type = T;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Append at the tail. O(1). Never fails; reports whether data was evicted.
    PushResult push(const T& item) noexcept {
        ++total_pushed_;
        if (count_ < Capacity) {
            storage_[wrap(head_ + count_)] = item;
            ++count_;
            return PushResult::Stored;
        }
        // Full: the slot at head_ holds the oldest element. Overwrite it and
        // advance head_ so the next-oldest becomes the new head.
        storage_[head_] = item;
        head_ = wrap(head_ + 1);
        ++overflow_count_;
        return PushResult::OverwroteOldest;
    }

    /// Remove the oldest element. O(1). Returns false when empty.
    bool pop(T& out) noexcept {
        if (count_ == 0) {
            return false;
        }
        out = storage_[head_];
        head_ = wrap(head_ + 1);
        --count_;
        return true;
    }

    /// Oldest element without removal, or nullptr when empty. O(1).
    [[nodiscard]] const T* peek_oldest() const noexcept {
        return count_ == 0 ? nullptr : &storage_[head_];
    }

    /// Newest element without removal, or nullptr when empty. O(1).
    [[nodiscard]] const T* peek_newest() const noexcept {
        return count_ == 0 ? nullptr : &storage_[wrap(head_ + count_ - 1)];
    }

    /// Element at logical index i, where 0 is the oldest. Caller checks i < size().
    [[nodiscard]] const T& at(std::size_t i) const noexcept { return storage_[wrap(head_ + i)]; }

    /// Bounded linear search, O(Capacity). Used for small recent-ID windows.
    template <typename Predicate>
    [[nodiscard]] bool any_of(Predicate&& pred) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (pred(at(i))) {
                return true;
            }
        }
        return false;
    }

    void clear() noexcept {
        head_ = 0;
        count_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool full() const noexcept { return count_ == Capacity; }
    [[nodiscard]] std::uint64_t total_pushed() const noexcept { return total_pushed_; }
    [[nodiscard]] std::uint64_t overflow_count() const noexcept { return overflow_count_; }
    [[nodiscard]] bool overflow_occurred() const noexcept { return overflow_count_ != 0; }

private:
    // For power-of-two capacities the compiler lowers this modulo to a mask.
    [[nodiscard]] static constexpr std::size_t wrap(std::size_t i) noexcept { return i % Capacity; }

    std::size_t head_ = 0;   ///< Index of the oldest element
    std::size_t count_ = 0;  ///< Number of live elements
    std::uint64_t total_pushed_ = 0;
    std::uint64_t overflow_count_ = 0;
    std::array<T, Capacity> storage_{};
};

}  // namespace ocas

#endif  // OCAS_INGESTION_CIRCULAR_BUFFER_HPP
