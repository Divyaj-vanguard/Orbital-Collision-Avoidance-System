// ============================================================================
// OCAS Module 5a: State Persistence (Memento pattern)
// ============================================================================
//   Spacecraft         Originator. Owns SpacecraftState; creates and restores
//                      mementos. Only it can read a memento's contents.
//   SpacecraftMemento  Opaque snapshot (state + the advisory it guards).
//   StateStack<T, N>   Fixed-capacity LIFO over std::array. O(1) push/pop.
//   StateCaretaker     Checkpoints before each staged advisory; on an external
//                      abort it restores in O(1) (last) or unwinds LIFO to the
//                      aborted advisory, undoing everything staged after it.
// ============================================================================
#ifndef OCAS_PERSISTENCE_STATE_STACK_HPP
#define OCAS_PERSISTENCE_STATE_STACK_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

template <typename T, std::size_t Capacity>
class StateStack {
    static_assert(Capacity > 0, "StateStack capacity must be non-zero");

public:
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// O(1). Returns false (and counts) when full; never overwrites.
    bool push(const T& item) noexcept {
        if (top_ == Capacity) {
            ++rejected_;
            return false;
        }
        data_[top_++] = item;
        return true;
    }

    /// O(1). Returns false when empty.
    bool pop(T& out) noexcept {
        if (top_ == 0) {
            return false;
        }
        out = data_[--top_];
        return true;
    }

    [[nodiscard]] const T* top() const noexcept { return top_ == 0 ? nullptr : &data_[top_ - 1]; }
    /// Element at depth i from the top (0 = top). Caller checks i < size().
    [[nodiscard]] const T& from_top(std::size_t i) const noexcept { return data_[top_ - 1 - i]; }

    void clear() noexcept { top_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return top_; }
    [[nodiscard]] bool empty() const noexcept { return top_ == 0; }
    [[nodiscard]] bool full() const noexcept { return top_ == Capacity; }
    [[nodiscard]] std::uint64_t rejected_count() const noexcept { return rejected_; }

private:
    std::array<T, Capacity> data_{};
    std::size_t top_ = 0;
    std::uint64_t rejected_ = 0;
};

class Spacecraft;

class SpacecraftMemento {
public:
    SpacecraftMemento() = default;
    [[nodiscard]] std::uint32_t alert_id() const noexcept { return alert_id_; }
    [[nodiscard]] std::uint64_t cycle() const noexcept { return cycle_; }

private:
    friend class Spacecraft;
    SpacecraftMemento(const SpacecraftState& s, std::uint32_t alert_id, std::uint64_t cycle) noexcept
        : state_(s), alert_id_(alert_id), cycle_(cycle) {}

    SpacecraftState state_{};
    std::uint32_t alert_id_ = 0;
    std::uint64_t cycle_ = 0;
};

class Spacecraft {
public:
    explicit Spacecraft(const SpacecraftState& initial = {}) noexcept : state_(initial) {}

    [[nodiscard]] SpacecraftMemento save(std::uint32_t alert_id, std::uint64_t cycle) const noexcept {
        return SpacecraftMemento(state_, alert_id, cycle);
    }
    void restore(const SpacecraftMemento& m) noexcept;

    /// Tentatively reserve delta-v for an advisory. False if the budget is short.
    bool reserve(const EvasionDirective& d) noexcept;
    /// Acknowledge all pending reservations.
    void acknowledge() noexcept;

    [[nodiscard]] const SpacecraftState& state() const noexcept { return state_; }

private:
    SpacecraftState state_;
};

class StateCaretaker {
public:
    static constexpr std::size_t kDepth = 16;

    /// O(1). False if the stack is full (caller must not stage the advisory).
    bool checkpoint(const Spacecraft& sc, std::uint32_t alert_id, std::uint64_t cycle) noexcept;
    /// O(1): restore the most recent checkpoint.
    bool rollback_last(Spacecraft& sc) noexcept;
    /// LIFO unwind to the checkpoint taken for alert_id, restoring the state
    /// that existed before it. Returns the number of checkpoints undone, or 0
    /// (and changes nothing) if alert_id is not on the stack. O(depth).
    std::size_t unwind_to(Spacecraft& sc, std::uint32_t alert_id) noexcept;
    /// Advisories acknowledged: discard all checkpoints.
    void commit_all() noexcept { stack_.clear(); }

    [[nodiscard]] std::size_t depth() const noexcept { return stack_.size(); }
    [[nodiscard]] std::uint64_t rollbacks() const noexcept { return rollbacks_; }

private:
    StateStack<SpacecraftMemento, kDepth> stack_;
    std::uint64_t rollbacks_ = 0;
};

}  // namespace ocas

#endif  // OCAS_PERSISTENCE_STATE_STACK_HPP
