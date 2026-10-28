#ifndef OCAS_PIPELINE_HPP
#define OCAS_PIPELINE_HPP

#include "neural.hpp"
#include "recorder.hpp"
#include "regime.hpp"
#include "risk.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// Heap order, true when a ranks below b: actionable first, then priority, then earlier TCA,
// then lower alert_id, then earlier sequence (a total order, so popping is deterministic).
bool ranks_below(const ScoredThreat& a, const ScoredThreat& b) noexcept;

// One cycle: drain the inbox, validate and score each CDM into a priority heap, pop
// actionable threats and stage a directive for each (reserving dv), then simulate the
// FCU acknowledgement: an abort of advisory k restores the budget from before k and
// undoes everything staged after it; the rest commit.
// initialize() is the only allocation; the cycle only uses reserved or fixed storage.
class Pipeline {
public:
    static constexpr std::size_t kInboxCapacity = 64;
    static constexpr std::size_t kAdvisedWindow = 256;
    static constexpr std::size_t kMaxStagedPerCycle = 16;

    bool initialize(const PipelineConfig& cfg, Recorder* recorder, std::string& error);
    // True when the inbox was full and the oldest CDM was dropped.
    bool ingest(const ConjunctionTelemetry& t);
    CycleStats run_cycle();
    void finish();

    [[nodiscard]] const CycleStats& totals() const noexcept { return totals_; }
    [[nodiscard]] const Regime& regime() const noexcept { return regime_; }
    [[nodiscard]] double dv_budget_mps() const noexcept { return dv_budget_; }
    [[nodiscard]] double mean_score_ns() const noexcept {
        return totals_.scored == 0 ? 0.0 : score_ns_ / static_cast<double>(totals_.scored);
    }

private:
    struct Staged {
        LedgerRecord record;
        double budget_before;
        std::uint64_t ordinal;
    };

    [[nodiscard]] bool already_advised(std::uint32_t alert_id) const noexcept;
    void score_inbox(CycleStats& stats);
    void issue_advisories(CycleStats& stats);
    void acknowledge_advisories(CycleStats& stats);

    PipelineConfig cfg_{};
    Regime regime_{};
    LstmModel lstm_;
    Recorder* rec_ = nullptr;

    std::vector<ConjunctionTelemetry> inbox_;
    std::vector<ScoredThreat> heap_;
    double dv_budget_ = 0.0;
    std::array<std::uint32_t, kAdvisedWindow> advised_{};  // last kAdvisedWindow resolved alert ids
    std::size_t advised_count_ = 0;
    std::array<Staged, kMaxStagedPerCycle> staged_{};
    std::size_t staged_count_ = 0;

    CycleStats totals_{};
    std::uint64_t cycle_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t staged_ordinal_ = 0;
    std::uint64_t overflows_ = 0;
    double score_ns_ = 0.0;
};

}  // namespace ocas

#endif
