// ============================================================================
// OCAS ML layer: per-event recurrent state store for the LSTM strategy.
// Fixed-capacity open-addressing table keyed by alert_id (event id). Lookup
// probes at most kProbe slots, so every operation is bounded. When a probe
// window is full, the least recently updated entry in it is evicted and the
// eviction is counted. No heap allocation.
// ============================================================================
#ifndef OCAS_ML_SEQUENCE_STORE_HPP
#define OCAS_ML_SEQUENCE_STORE_HPP

#include "ml/neural.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas::ml {

template <std::size_t Capacity>
class SequenceStateStore {
    static_assert(Capacity >= 16 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two >= 16");

public:
    static constexpr std::size_t kProbe = 16;

    /// State for alert_id, inserting a zeroed state if absent. Never fails.
    LstmState& acquire(std::uint32_t alert_id, std::size_t* steps_out = nullptr) noexcept {
        const std::size_t home = hash(alert_id);
        std::size_t victim = home;
        for (std::size_t k = 0; k < kProbe; ++k) {
            Entry& e = slots_[(home + k) & (Capacity - 1)];
            if (e.used && e.key == alert_id) {
                e.last_touch = ++clock_;
                ++e.steps;
                if (steps_out) *steps_out = e.steps;
                return e.state;
            }
            if (!e.used) {
                victim = (home + k) & (Capacity - 1);
                break;
            }
            if (e.last_touch < slots_[victim].last_touch) victim = (home + k) & (Capacity - 1);
        }
        Entry& e = slots_[victim];
        if (e.used) ++evictions_;
        else ++size_;
        e = Entry{};
        e.used = true;
        e.key = alert_id;
        e.last_touch = ++clock_;
        e.steps = 1;
        if (steps_out) *steps_out = 1;
        return e.state;
    }

    void clear() noexcept {
        slots_ = {};
        size_ = 0;
        evictions_ = 0;
        clock_ = 0;
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }

private:
    struct Entry {
        LstmState state{};
        std::uint64_t last_touch = 0;
        std::uint32_t key = 0;
        std::uint32_t steps = 0;
        bool used = false;
    };

    static std::size_t hash(std::uint32_t k) noexcept {
        std::uint64_t z = k + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        return static_cast<std::size_t>(z ^ (z >> 31)) & (Capacity - 1);
    }

    std::array<Entry, Capacity> slots_{};
    std::size_t size_ = 0;
    std::uint64_t evictions_ = 0;
    std::uint64_t clock_ = 0;
};

}  // namespace ocas::ml

#endif  // OCAS_ML_SEQUENCE_STORE_HPP
