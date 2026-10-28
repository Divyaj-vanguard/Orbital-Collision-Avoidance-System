#ifndef OCAS_PRIORITY_MAX_HEAP_HPP
#define OCAS_PRIORITY_MAX_HEAP_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

// Binary max-heap over a fixed array. cmp(a, b) == true means a ranks below b.
// When full, push rejects and counts; it never evicts.
template <typename T, std::size_t Capacity, typename Compare>
class MaxHeap {
    static_assert(Capacity > 0);

public:
    bool push(const T& item) noexcept {
        if (size_ == Capacity) {
            ++rejected_;
            return false;
        }
        std::size_t hole = size_++;
        while (hole > 0) {
            const std::size_t parent = (hole - 1) / 2;
            if (!cmp_(data_[parent], item)) break;
            data_[hole] = data_[parent];
            hole = parent;
        }
        data_[hole] = item;
        return true;
    }

    bool pop(T& out) noexcept {
        if (size_ == 0) return false;
        out = data_[0];
        if (--size_ == 0) return true;
        const T last = data_[size_];
        std::size_t hole = 0;
        for (;;) {
            std::size_t child = 2 * hole + 1;
            if (child >= size_) break;
            if (child + 1 < size_ && cmp_(data_[child], data_[child + 1])) ++child;
            if (!cmp_(last, data_[child])) break;
            data_[hole] = data_[child];
            hole = child;
        }
        data_[hole] = last;
        return true;
    }

    void clear() noexcept { size_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t rejected_count() const noexcept { return rejected_; }

    [[nodiscard]] bool is_valid_heap() const noexcept {
        for (std::size_t i = 1; i < size_; ++i) {
            if (cmp_(data_[(i - 1) / 2], data_[i])) return false;
        }
        return true;
    }

private:
    std::array<T, Capacity> data_{};
    std::size_t size_ = 0;
    std::uint64_t rejected_ = 0;
    Compare cmp_{};
};

// Highest first: actionable, then priority, then earlier TCA, then lower alert_id, then sequence.
struct ThreatComparator {
    bool operator()(const ScoredThreat& a, const ScoredThreat& b) const noexcept {
        if (a.risk.actionable != b.risk.actionable) return !a.risk.actionable;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.telemetry.time_to_tca_sec != b.telemetry.time_to_tca_sec)
            return a.telemetry.time_to_tca_sec > b.telemetry.time_to_tca_sec;
        if (a.telemetry.alert_id != b.telemetry.alert_id) return a.telemetry.alert_id > b.telemetry.alert_id;
        return a.sequence > b.sequence;
    }
};

inline constexpr std::size_t kThreatHeapCapacity = 64;
using ThreatHeap = MaxHeap<ScoredThreat, kThreatHeapCapacity, ThreatComparator>;

}  // namespace ocas

#endif
