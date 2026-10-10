// ============================================================================
// OCAS Observer pattern: pipeline events fan out to a fixed set of observers
// (terminal telemetry, flight audit logger, ...). Registry is a fixed array of
// non-owning pointers, so notification never allocates.
// ============================================================================
#ifndef OCAS_CORE_OBSERVER_HPP
#define OCAS_CORE_OBSERVER_HPP

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

enum class InputRejectReason : std::uint8_t { NonFinite, NegativeDistance, PastTca };
const char* to_string(InputRejectReason r) noexcept;

struct SessionInfo {
    OrbitalRegime regime = OrbitalRegime::LEO;
    double altitude_km = 0.0;
    double orbital_period_s = 0.0;
    double dv_budget_mps = 0.0;
    double decision_threshold = 0.0;
    std::uint32_t abort_every_n = 0;
    const char* input_label = "";
};

struct CycleStats {
    std::uint64_t cycle = 0;
    std::size_t ingested = 0;
    std::size_t rejected_input = 0;
    std::size_t scored = 0;
    std::size_t actionable = 0;
    std::size_t logistic_used = 0;
    std::size_t fallback_used = 0;
    std::size_t directives = 0;
    std::size_t committed = 0;
    std::size_t rolled_back = 0;
    std::size_t rejected_dv_cap = 0;
    std::size_t rejected_budget = 0;
    std::size_t suppressed_duplicates = 0;
    std::size_t monitored = 0;  ///< Scored but not advised this cycle
    std::uint64_t ingest_overflow_total = 0;
    std::uint64_t heap_rejected_total = 0;
    double cycle_latency_us = 0.0;
    double dv_budget_remaining_mps = 0.0;
};

class IPipelineObserver {
public:
    virtual ~IPipelineObserver() = default;
    virtual void on_session_start(const SessionInfo&) {}
    virtual void on_input_rejected(const ConjunctionTelemetry&, InputRejectReason) {}
    virtual void on_threat_scored(const ScoredThreat&) {}
    virtual void on_advisory(const LedgerRecord&) {}
    virtual void on_advisory_resolved(const LedgerRecord&) {}
    virtual void on_rollback(std::uint32_t /*abort_alert_id*/, std::size_t /*unwound*/, const SpacecraftState&) {}
    virtual void on_cycle_end(const CycleStats&) {}
    virtual void on_session_end(const CycleStats& /*totals*/) {}

protected:
    IPipelineObserver() = default;
    IPipelineObserver(const IPipelineObserver&) = default;
    IPipelineObserver& operator=(const IPipelineObserver&) = default;
};

template <std::size_t MaxObservers>
class ObserverRegistry {
public:
    bool attach(IPipelineObserver* o) noexcept {
        if (o == nullptr || count_ == MaxObservers) {
            return false;
        }
        observers_[count_++] = o;
        return true;
    }

    template <typename Fn>
    void notify(Fn&& fn) const {
        for (std::size_t i = 0; i < count_; ++i) {
            fn(*observers_[i]);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    std::array<IPipelineObserver*, MaxObservers> observers_{};
    std::size_t count_ = 0;
};

}  // namespace ocas

#endif  // OCAS_CORE_OBSERVER_HPP
