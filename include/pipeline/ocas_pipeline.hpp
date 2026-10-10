// ============================================================================
// OCAS Module 6: End-to-end orchestrator
// ============================================================================
// One cycle:
//   1. drain the ingestion ring buffer
//   2. validate each CDM, score it (RiskClassifier -> IRiskStrategy vtable),
//      add regime risk (OrbitalRegimeStrategy vtable), push to the max-heap
//   3. pop the heap in priority order; for each actionable, not-yet-advised
//      threat formulate an EvasionDirective, checkpoint the spacecraft state
//      (Memento) and reserve delta-v, record it in the mission ledger
//   4. simulated OEM flight-bus acknowledgement: an external abort unwinds
//      the state stack to the aborted advisory; the rest are committed
//   5. publish events to observers (terminal telemetry, audit logger)
// initialize() is the only method that allocates (strategy objects). Every
// container the cycle touches is a fixed-capacity member.
// ============================================================================
#ifndef OCAS_PIPELINE_OCAS_PIPELINE_HPP
#define OCAS_PIPELINE_OCAS_PIPELINE_HPP

#include "core/latency_profiler.hpp"
#include "core/observer.hpp"
#include "core/types.hpp"
#include "ingestion/circular_buffer.hpp"
#include "persistence/mission_ledger.hpp"
#include "persistence/state_stack.hpp"
#include "priority/max_heap.hpp"
#include "regime/regime_strategy.hpp"
#include "risk/risk_strategy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ocas {

struct PipelineConfig {
    double altitude_km = 550.0;
    double dv_budget_mps = 10.0;               ///< Collision-avoidance delta-v allocation
    std::size_t max_advisories_per_cycle = 8;
    std::uint32_t abort_every_n = 0;           ///< Simulated external abort on every Nth staged advisory; 0 = off
    double priority_model_weight = 0.7;        ///< priority = w * model score + (1 - w) * regime risk
    LogisticModelParams logistic{};
    ConservativeParams conservative{};
    const char* input_label = "";
};

class OcasPipeline {
public:
    static constexpr std::size_t kIngestCapacity = 64;
    static constexpr std::size_t kAdvisedWindow = 256;   ///< Recently resolved alert_ids (dedup)
    static constexpr std::size_t kMaxStagedPerCycle = StateCaretaker::kDepth;
    static constexpr std::size_t kMaxObservers = 4;

    OcasPipeline() = default;
    OcasPipeline(const OcasPipeline&) = delete;
    OcasPipeline& operator=(const OcasPipeline&) = delete;

    /// Init-time setup. Builds the regime strategy through RegimeFactory.
    bool initialize(const PipelineConfig& cfg, std::string& error);
    bool attach(IPipelineObserver* o) noexcept { return observers_.attach(o); }

    /// Producer side, O(1). Overwrites the oldest buffered CDM when full.
    PushResult ingest(const ConjunctionTelemetry& t) noexcept { return ingest_.push(t); }

    /// Consumer side: one deterministic processing cycle.
    CycleStats run_cycle();

    /// Emit the session-end event with lifetime totals.
    void finish();

    [[nodiscard]] const CycleStats& totals() const noexcept { return totals_; }
    [[nodiscard]] const MissionLedger& ledger() const noexcept { return ledger_; }
    [[nodiscard]] const Spacecraft& spacecraft() const noexcept { return spacecraft_; }
    [[nodiscard]] const StateCaretaker& caretaker() const noexcept { return caretaker_; }
    [[nodiscard]] const OrbitalRegimeStrategy& regime() const noexcept { return *regime_; }
    [[nodiscard]] const RiskClassifier& classifier() const noexcept { return *classifier_; }
    [[nodiscard]] const CircularBuffer<ConjunctionTelemetry, kIngestCapacity>& ingest_buffer() const noexcept {
        return ingest_;
    }
    [[nodiscard]] const LatencyProfiler& cdm_latency() const noexcept { return cdm_latency_; }
    [[nodiscard]] const LatencyProfiler& advisory_latency() const noexcept { return advisory_latency_; }
    [[nodiscard]] const LatencyProfiler& cycle_latency() const noexcept { return cycle_latency_; }
    [[nodiscard]] std::size_t heap_peak() const noexcept { return heap_peak_; }

private:
    struct Staged {
        MissionLedger::Handle handle;
        std::uint32_t alert_id;
        std::uint64_t ordinal;  ///< Lifetime count of staged advisories
    };

    [[nodiscard]] bool already_advised(std::uint32_t alert_id) const noexcept;
    void score_buffered(CycleStats& stats);
    void issue_advisories(CycleStats& stats);
    void acknowledge_advisories(CycleStats& stats);
    void resolve(MissionLedger::Handle h, AdvisoryStatus s);
    void accumulate(const CycleStats& c) noexcept;

    PipelineConfig cfg_{};
    std::unique_ptr<OrbitalRegimeStrategy> regime_;
    std::optional<RiskClassifier> classifier_;

    CircularBuffer<ConjunctionTelemetry, kIngestCapacity> ingest_;
    ThreatHeap heap_;
    Spacecraft spacecraft_;
    StateCaretaker caretaker_;
    MissionLedger ledger_;
    CircularBuffer<std::uint32_t, kAdvisedWindow> advised_;
    ObserverRegistry<kMaxObservers> observers_;

    std::array<Staged, kMaxStagedPerCycle> staged_{};
    std::size_t staged_count_ = 0;
    std::array<std::uint32_t, kMaxStagedPerCycle> cycle_ids_{};  ///< Every alert decided this cycle
    std::size_t cycle_id_count_ = 0;

    LatencyProfiler cdm_latency_;
    LatencyProfiler advisory_latency_;
    LatencyProfiler cycle_latency_;

    CycleStats totals_{};
    std::uint64_t cycle_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t staged_ordinal_ = 0;
    std::size_t heap_peak_ = 0;
};

}  // namespace ocas

#endif  // OCAS_PIPELINE_OCAS_PIPELINE_HPP
