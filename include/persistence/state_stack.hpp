#ifndef OCAS_PERSISTENCE_STATE_STACK_HPP
#define OCAS_PERSISTENCE_STATE_STACK_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

// Fixed-capacity LIFO. push refuses (and counts) when full; never overwrites.
template <typename T, std::size_t Capacity>
class StateStack {
    static_assert(Capacity > 0);

public:
    bool push(const T& item) noexcept {
        if (top_ == Capacity) {
            ++rejected_;
            return false;
        }
        data_[top_++] = item;
        return true;
    }

    bool pop(T& out) noexcept {
        if (top_ == 0) return false;
        out = data_[--top_];
        return true;
    }

    // 0 is the top. Caller checks i < size().
    [[nodiscard]] const T& from_top(std::size_t i) const noexcept { return data_[top_ - 1 - i]; }

    void clear() noexcept { top_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return top_; }
    [[nodiscard]] std::uint64_t rejected_count() const noexcept { return rejected_; }

private:
    std::array<T, Capacity> data_{};
    std::size_t top_ = 0;
    std::uint64_t rejected_ = 0;
};

class Spacecraft;

// Memento: opaque snapshot; only Spacecraft (the originator) can read the state.
class SpacecraftMemento {
public:
    SpacecraftMemento() = default;
    [[nodiscard]] std::uint32_t alert_id() const noexcept { return alert_id_; }

private:
    friend class Spacecraft;
    SpacecraftMemento(const SpacecraftState& s, std::uint32_t alert_id) noexcept : state_(s), alert_id_(alert_id) {}

    SpacecraftState state_{};
    std::uint32_t alert_id_ = 0;
};

class Spacecraft {
public:
    explicit Spacecraft(const SpacecraftState& initial = {}) noexcept : state_(initial) {}

    [[nodiscard]] SpacecraftMemento save(std::uint32_t alert_id) const noexcept { return {state_, alert_id}; }
    void restore(const SpacecraftMemento& m) noexcept { state_ = m.state_; }

    // False if the budget is short.
    bool reserve(const EvasionDirective& d) noexcept;

    [[nodiscard]] const SpacecraftState& state() const noexcept { return state_; }

private:
    SpacecraftState state_;
};

class StateCaretaker {
public:
    static constexpr std::size_t kDepth = 16;

    bool checkpoint(const Spacecraft& sc, std::uint32_t alert_id) noexcept;
    // Pops back to alert_id's checkpoint and restores the state before it.
    // Returns checkpoints undone, or 0 (no change) if alert_id is not on the stack.
    std::size_t unwind_to(Spacecraft& sc, std::uint32_t alert_id) noexcept;
    void commit_all() noexcept { stack_.clear(); }

    [[nodiscard]] std::size_t depth() const noexcept { return stack_.size(); }
    [[nodiscard]] std::uint64_t rollbacks() const noexcept { return rollbacks_; }

private:
    StateStack<SpacecraftMemento, kDepth> stack_;
    std::uint64_t rollbacks_ = 0;
};

}  // namespace ocas

#endif
