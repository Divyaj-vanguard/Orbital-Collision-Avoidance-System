// ============================================================================
// OCAS Module 3: Priority Ranking Engine
// MaxHeap<T, Capacity, Compare>: binary max-heap over a fixed array.
// ============================================================================
//   push      O(log n)   sift-up with a moving "hole" (one write per level)
//   pop       O(log n)   sift-down with a moving "hole"
//   top       O(1)
// Compare follows the std::less convention: cmp(a, b) == true means a ranks
// BELOW b, so the root is the element no other element outranks.
// When full, push() rejects the new element and counts the rejection; it
// never evicts silently.
// ============================================================================
#ifndef OCAS_PRIORITY_MAX_HEAP_HPP
#define OCAS_PRIORITY_MAX_HEAP_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ocas {

template <typename T, std::size_t Capacity, typename Compare>
class MaxHeap {
    static_assert(Capacity > 0, "MaxHeap capacity must be non-zero");

public:
    explicit MaxHeap(Compare cmp = Compare{}) noexcept : cmp_(cmp) {}

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    bool push(const T& item) noexcept {
        if (size_ == Capacity) {
            ++rejected_;
            return false;
        }
        T* const a = data_.data();
        std::size_t hole = size_++;
        while (hole > 0) {
            const std::size_t parent = (hole - 1) / 2;
            if (!cmp_(a[parent], item)) {
                break;
            }
            a[hole] = a[parent];
            hole = parent;
        }
        a[hole] = item;
        return true;
    }

    bool pop(T& out) noexcept {
        if (size_ == 0) {
            return false;
        }
        T* const a = data_.data();
        out = a[0];
        --size_;
        if (size_ == 0) {
            return true;
        }
        const T last = a[size_];
        std::size_t hole = 0;
        for (;;) {
            std::size_t child = 2 * hole + 1;
            if (child >= size_) {
                break;
            }
            if (child + 1 < size_ && cmp_(a[child], a[child + 1])) {
                ++child;
            }
            if (!cmp_(last, a[child])) {
                break;
            }
            a[hole] = a[child];
            hole = child;
        }
        a[hole] = last;
        return true;
    }

    [[nodiscard]] const T* top() const noexcept { return size_ == 0 ? nullptr : data_.data(); }

    /// Read-only view of the raw heap array (heap order, not sorted order).
    [[nodiscard]] const T* data() const noexcept { return data_.data(); }

    void clear() noexcept { size_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }
    [[nodiscard]] std::uint64_t rejected_count() const noexcept { return rejected_; }

    /// Verifies the heap property over the whole array. O(n); tests only.
    [[nodiscard]] bool is_valid_heap() const noexcept {
        for (std::size_t i = 1; i < size_; ++i) {
            if (cmp_(data_[(i - 1) / 2], data_[i])) {
                return false;
            }
        }
        return true;
    }

private:
    std::array<T, Capacity> data_{};
    std::size_t size_ = 0;
    std::uint64_t rejected_ = 0;
    Compare cmp_;
};

/// Threat ordering, highest first:
///   1. actionable threats outrank non-actionable ones
///   2. higher composite priority
///   3. earlier time to closest approach
///   4. lower alert_id, then lower ingest sequence (total, deterministic order)
struct ThreatComparator {
    bool operator()(const ScoredThreat& a, const ScoredThreat& b) const noexcept;
};

inline constexpr std::size_t kThreatHeapCapacity = 64;
using ThreatHeap = MaxHeap<ScoredThreat, kThreatHeapCapacity, ThreatComparator>;

// Instantiated once in src/priority/max_heap.cpp.
extern template class MaxHeap<ScoredThreat, kThreatHeapCapacity, ThreatComparator>;

}  // namespace ocas

#endif  // OCAS_PRIORITY_MAX_HEAP_HPP
