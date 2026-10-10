// OCAS Module 5a implementation: Memento originator and caretaker.
#include "persistence/state_stack.hpp"

namespace ocas {

void Spacecraft::restore(const SpacecraftMemento& m) noexcept {
    state_ = m.state_;
    state_.status_flags |= status::kRollback;
}

bool Spacecraft::reserve(const EvasionDirective& d) noexcept {
    if (d.delta_v_mps > state_.dv_budget_mps) {
        return false;
    }
    state_.dv_budget_mps -= d.delta_v_mps;
    state_.dv_reserved_mps += d.delta_v_mps;
    ++state_.advisories_reserved;
    state_.status_flags |= status::kAdvisoryPending;
    return true;
}

void Spacecraft::acknowledge() noexcept { state_.status_flags &= ~status::kAdvisoryPending; }

bool StateCaretaker::checkpoint(const Spacecraft& sc, std::uint32_t alert_id, std::uint64_t cycle) noexcept {
    return stack_.push(sc.save(alert_id, cycle));
}

bool StateCaretaker::rollback_last(Spacecraft& sc) noexcept {
    SpacecraftMemento m;
    if (!stack_.pop(m)) {
        return false;
    }
    sc.restore(m);
    ++rollbacks_;
    return true;
}

std::size_t StateCaretaker::unwind_to(Spacecraft& sc, std::uint32_t alert_id) noexcept {
    std::size_t depth_of_target = 0;
    bool found = false;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (stack_.from_top(i).alert_id() == alert_id) {
            depth_of_target = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return 0;
    }
    SpacecraftMemento m;
    for (std::size_t i = 0; i <= depth_of_target; ++i) {
        stack_.pop(m);
    }
    sc.restore(m);  // m is now the target's checkpoint: the state before it was staged
    const std::size_t undone = depth_of_target + 1;
    rollbacks_ += undone;
    return undone;
}

}  // namespace ocas
