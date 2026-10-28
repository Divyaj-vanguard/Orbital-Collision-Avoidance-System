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
    double dv_budget_mps = 10.0;
    std::size_t max_advisories_per_cycle = 8;
    std::uint32_t abort_every_n = 0;     // simulated abort of every Nth staged advisory; 0 = off
    double priority_model_weight = 0.7;  // priority = w * model score + (1 - w) * regime risk
    ml::LstmParams lstm{};
    const char* input_label = "";
};

// One cycle: drain the ring buffer, validate and score each CDM into the heap, pop
// actionable threats and stage directives (Memento checkpoint + dv reservation),
// then simulate the FCU acknowledgement: an abort unwinds the stack to the aborted
// advisory, everything else commits.
// initialize() is the only allocation; the cycle touches fixed-capacity members only.
class OcasPipeline {
public:
    static constexpr std::size_t kIngestCapacity = 64;
    static constexpr std::size_t kAdvisedWindow = 256;
    static constexpr std::size_t kMaxStagedPerCycle = StateCaretaker::kDepth;
    static constexpr std::size_t kMaxObservers = 4;

    OcasPipeline() = default;
    OcasPipeline(const OcasPipeline&) = delete;
    OcasPipeline& operator=(const OcasPipeline&) = delete;

    bool initialize(const PipelineConfig& cfg, std::string& error);
    bool attach(IPipelineObserver* o) noexcept { return observers_.attach(o); }
    PushResult ingest(const ConjunctionTelemetry& t) noexcept { return ingest_.push(t); }
    CycleStats run_cycle();
    void finish();

    [[nodiscard]] const CycleStats& totals() const noexcept { return totals_; }
    [[nodiscard]] const MissionLedger& ledger() const noexcept { return ledger_; }
    [[nodiscard]] const Spacecraft& spacecraft() const noexcept { return spacecraft_; }
    [[nodiscard]] const StateCaretaker& caretaker() const noexcept { return caretaker_; }
    [[nodiscard]] const OrbitalRegimeStrategy& regime() const noexcept { return *regime_; }
    [[nodiscard]] const LatencyProfiler& cdm_latency() const noexcept { return cdm_latency_; }
    [[nodiscard]] const LatencyProfiler& cycle_latency() const noexcept { return cycle_latency_; }

private:
    struct Staged {
        LedgerRecord* record;
        std::uint32_t alert_id;
        std::uint64_t ordinal;
    };

    [[nodiscard]] bool already_advised(std::uint32_t alert_id) const noexcept;
    void score_buffered(CycleStats& stats);
    void issue_advisories(CycleStats& stats);
    void acknowledge_advisories(CycleStats& stats);
    void resolve(LedgerRecord& r, AdvisoryStatus s);
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
    std::array<std::uint32_t, kMaxStagedPerCycle> cycle_ids_{};
    std::size_t cycle_id_count_ = 0;

    LatencyProfiler cdm_latency_;
    LatencyProfiler cycle_latency_;

    CycleStats totals_{};
    std::uint64_t cycle_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t staged_ordinal_ = 0;
};

}  // namespace ocas

#endif
