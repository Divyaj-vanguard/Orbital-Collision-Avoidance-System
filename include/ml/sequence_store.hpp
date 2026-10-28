#ifndef OCAS_ML_SEQUENCE_STORE_HPP
#define OCAS_ML_SEQUENCE_STORE_HPP

#include "ml/neural.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas::ml {

// Per-event LSTM state, open addressing keyed by alert_id. Probes at most kProbe slots;
// a full window evicts its least recently used entry and counts it.
template <std::size_t Capacity>
class SequenceStateStore {
    static_assert(Capacity >= 16 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two >= 16");

public:
    static constexpr std::size_t kProbe = 16;

    LstmState& acquire(std::uint32_t alert_id) noexcept {
        const std::size_t home = hash(alert_id);
        std::size_t victim = home;
        for (std::size_t k = 0; k < kProbe; ++k) {
            const std::size_t i = (home + k) & (Capacity - 1);
            Entry& e = slots_[i];
            if (e.used && e.key == alert_id) {
                e.last_touch = ++clock_;
                return e.state;
            }
            if (!e.used) {
                victim = i;
                break;
            }
            if (e.last_touch < slots_[victim].last_touch) victim = i;
        }
        Entry& e = slots_[victim];
        if (e.used) ++evictions_;
        else ++size_;
        e = Entry{};
        e.used = true;
        e.key = alert_id;
        e.last_touch = ++clock_;
        return e.state;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t evictions() const noexcept { return evictions_; }

private:
    struct Entry {
        LstmState state{};
        std::uint64_t last_touch = 0;
        std::uint32_t key = 0;
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

#endif
