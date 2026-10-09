// OCAS Module 3: ThreatComparator and the single explicit ThreatHeap instantiation.
#include "priority/max_heap.hpp"

namespace ocas {

bool ThreatComparator::operator()(const ScoredThreat& a, const ScoredThreat& b) const noexcept {
    // Returns true when a ranks BELOW b.
    if (a.risk.actionable != b.risk.actionable) {
        return !a.risk.actionable;
    }
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    if (a.telemetry.time_to_tca_sec != b.telemetry.time_to_tca_sec) {
        return a.telemetry.time_to_tca_sec > b.telemetry.time_to_tca_sec;
    }
    if (a.telemetry.alert_id != b.telemetry.alert_id) {
        return a.telemetry.alert_id > b.telemetry.alert_id;
    }
    return a.sequence > b.sequence;
}

template class MaxHeap<ScoredThreat, kThreatHeapCapacity, ThreatComparator>;

}  // namespace ocas
