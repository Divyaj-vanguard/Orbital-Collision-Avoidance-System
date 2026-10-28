#include "persistence/state_stack.hpp"

namespace ocas {

bool Spacecraft::reserve(const EvasionDirective& d) noexcept {
    if (d.delta_v_mps > state_.dv_budget_mps) return false;
    state_.dv_budget_mps -= d.delta_v_mps;
    return true;
}

bool StateCaretaker::checkpoint(const Spacecraft& sc, std::uint32_t alert_id) noexcept {
    return stack_.push(sc.save(alert_id));
}

std::size_t StateCaretaker::unwind_to(Spacecraft& sc, std::uint32_t alert_id) noexcept {
    std::size_t depth = 0;
    while (depth < stack_.size() && stack_.from_top(depth).alert_id() != alert_id) ++depth;
    if (depth == stack_.size()) return 0;
    SpacecraftMemento m;
    for (std::size_t i = 0; i <= depth; ++i) stack_.pop(m);
    sc.restore(m);
    rollbacks_ += depth + 1;
    return depth + 1;
}

}  // namespace ocas
