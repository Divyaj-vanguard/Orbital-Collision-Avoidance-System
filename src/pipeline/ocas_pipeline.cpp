// OCAS Module 6 implementation: the cyclic processing loop.
#include "pipeline/ocas_pipeline.hpp"

#include <cmath>

namespace ocas {

namespace {

bool validate(const ConjunctionTelemetry& t, InputRejectReason& why) noexcept {
    if (!std::isfinite(t.miss_distance_m) || !std::isfinite(t.relative_speed_km_s) ||
        !std::isfinite(t.time_to_tca_sec) || t.relative_speed_km_s < 0.0) {
        why = InputRejectReason::NonFinite;
        return false;
    }
    if (t.miss_distance_m < 0.0) {
        why = InputRejectReason::NegativeDistance;
        return false;
    }
    if (t.time_to_tca_sec <= 0.0) {
        why = InputRejectReason::PastTca;
        return false;
    }
    return true;
}

}  // namespace

bool OcasPipeline::initialize(const PipelineConfig& cfg, std::string& error) {
    if (cfg.max_advisories_per_cycle == 0 || cfg.max_advisories_per_cycle > kMaxStagedPerCycle) {
        error = "max_advisories_per_cycle must be in [1, " + std::to_string(kMaxStagedPerCycle) + "]";
        return false;
    }
    if (!(cfg.dv_budget_mps >= 0.0) || !(cfg.priority_model_weight >= 0.0 && cfg.priority_model_weight <= 1.0)) {
        error = "dv_budget_mps must be >= 0 and priority_model_weight in [0, 1]";
        return false;
    }
    regime_ = RegimeFactory::create_regime(cfg.altitude_km);
    if (!regime_) {
        error = "altitude " + std::to_string(cfg.altitude_km) + " km is not a valid orbital altitude";
        return false;
    }
    cfg_ = cfg;
    // The logistic model was trained on ESA LEO data only.
    classifier_.emplace(cfg.logistic, cfg.conservative, regime_->regime() == OrbitalRegime::LEO);

    SpacecraftState s{};
    s.altitude_km = cfg.altitude_km;
    s.dv_budget_mps = cfg.dv_budget_mps;
    spacecraft_ = Spacecraft(s);

    SessionInfo info{};
    info.regime = regime_->regime();
    info.altitude_km = cfg.altitude_km;
    info.orbital_period_s = regime_->orbital_period_s();
    info.dv_budget_mps = cfg.dv_budget_mps;
    info.decision_threshold = cfg.logistic.decision_threshold;
    info.abort_every_n = cfg.abort_every_n;
    info.input_label = cfg.input_label;
    observers_.notify([&](IPipelineObserver& o) { o.on_session_start(info); });
    return true;
}

bool OcasPipeline::already_advised(std::uint32_t alert_id) const noexcept {
    for (std::size_t i = 0; i < cycle_id_count_; ++i) {
        if (cycle_ids_[i] == alert_id) return true;
    }
    return advised_.any_of([alert_id](std::uint32_t id) { return id == alert_id; });
}

void OcasPipeline::score_buffered(CycleStats& stats) {
    ConjunctionTelemetry t;
    while (ingest_.pop(t)) {
        const auto start = SteadyClock::now();
        ++stats.ingested;
        InputRejectReason why{};
        if (!validate(t, why)) {
            ++stats.rejected_input;
            observers_.notify([&](IPipelineObserver& o) { o.on_input_rejected(t, why); });
            continue;
        }
        ScoredThreat s{};
        s.telemetry = t;
        s.sequence = ++sequence_;
        s.risk = classifier_->assess(t);
        s.regime_risk = regime_->calculate_regime_risk(t);
        s.priority = cfg_.priority_model_weight * s.risk.score + (1.0 - cfg_.priority_model_weight) * s.regime_risk;
        heap_.push(s);
        cdm_latency_.record(elapsed_ns(start));

        ++stats.scored;
        if (s.risk.model == RiskModel::LogisticRegression) {
            ++stats.logistic_used;
        } else {
            ++stats.fallback_used;
        }
        if (s.risk.actionable) ++stats.actionable;
        observers_.notify([&](IPipelineObserver& o) { o.on_threat_scored(s); });
    }
    if (heap_.size() > heap_peak_) heap_peak_ = heap_.size();
}

void OcasPipeline::issue_advisories(CycleStats& stats) {
    ScoredThreat top{};
    while (stats.directives < cfg_.max_advisories_per_cycle && heap_.pop(top)) {
        if (!top.risk.actionable) {
            ++stats.monitored;  // heap order puts every actionable threat first
            break;
        }
        const std::uint32_t id = top.telemetry.alert_id;
        if (already_advised(id)) {
            ++stats.suppressed_duplicates;
            continue;
        }
        const auto start = SteadyClock::now();
        LedgerRecord rec{};
        rec.cycle = cycle_;
        rec.directive = regime_->formulate_evasion_directive(top.telemetry);
        rec.risk_score = top.risk.score;
        rec.priority = top.priority;
        rec.model = top.risk.model;

        if (!rec.directive.within_regime_dv_cap) {
            rec.status = AdvisoryStatus::RejectedDvCap;
            ++stats.rejected_dv_cap;
        } else if (rec.directive.delta_v_mps > spacecraft_.state().dv_budget_mps) {
            rec.status = AdvisoryStatus::RejectedBudget;
            ++stats.rejected_budget;
        } else {
            caretaker_.checkpoint(spacecraft_, id, cycle_);  // depth >= max per cycle, cannot fail
            spacecraft_.reserve(rec.directive);
            rec.status = AdvisoryStatus::Staged;
        }
        const MissionLedger::Handle h = ledger_.append(rec);
        if (rec.status == AdvisoryStatus::Staged) {
            staged_[staged_count_++] = Staged{h, id, ++staged_ordinal_};
        } else {
            advised_.push(id);  // escalated to ground; do not re-alert on every CDM update
        }
        cycle_ids_[cycle_id_count_++] = id;
        ++stats.directives;
        advisory_latency_.record(elapsed_ns(start));
        observers_.notify([&](IPipelineObserver& o) { o.on_advisory(rec); });
    }
    stats.monitored += heap_.size();
}

void OcasPipeline::resolve(MissionLedger::Handle h, AdvisoryStatus s) {
    ledger_.set_status(h, s);
    if (const LedgerRecord* r = ledger_.get(h)) {
        observers_.notify([&](IPipelineObserver& o) { o.on_advisory_resolved(*r); });
    }
}

void OcasPipeline::acknowledge_advisories(CycleStats& stats) {
    // Simulated OEM flight-bus acknowledgement. An abort of advisory k also
    // invalidates every advisory staged after k: they were sized against a
    // state that included k's delta-v reservation.
    std::size_t abort_at = staged_count_;
    if (cfg_.abort_every_n != 0) {
        for (std::size_t i = 0; i < staged_count_; ++i) {
            if (staged_[i].ordinal % cfg_.abort_every_n == 0) {
                abort_at = i;
                break;
            }
        }
    }
    if (abort_at < staged_count_) {
        const std::uint32_t aborted_id = staged_[abort_at].alert_id;
        const std::size_t unwound = caretaker_.unwind_to(spacecraft_, aborted_id);
        for (std::size_t i = abort_at; i < staged_count_; ++i) {
            resolve(staged_[i].handle, AdvisoryStatus::RolledBack);
        }
        stats.rolled_back += unwound;
        observers_.notify([&](IPipelineObserver& o) { o.on_rollback(aborted_id, unwound, spacecraft_.state()); });
    }
    for (std::size_t i = 0; i < abort_at; ++i) {
        resolve(staged_[i].handle, AdvisoryStatus::Committed);
        advised_.push(staged_[i].alert_id);
        ++stats.committed;
    }
    caretaker_.commit_all();
    spacecraft_.acknowledge();
}

CycleStats OcasPipeline::run_cycle() {
    const auto start = SteadyClock::now();
    CycleStats stats{};
    stats.cycle = ++cycle_;
    heap_.clear();
    staged_count_ = 0;
    cycle_id_count_ = 0;

    score_buffered(stats);
    issue_advisories(stats);
    acknowledge_advisories(stats);
    heap_.clear();  // unadvised threats are re-scored when their next CDM update arrives

    stats.ingest_overflow_total = ingest_.overflow_count();
    stats.heap_rejected_total = heap_.rejected_count();
    stats.dv_budget_remaining_mps = spacecraft_.state().dv_budget_mps;
    const std::uint64_t ns = elapsed_ns(start);
    cycle_latency_.record(ns);
    stats.cycle_latency_us = static_cast<double>(ns) / 1000.0;

    accumulate(stats);
    observers_.notify([&](IPipelineObserver& o) { o.on_cycle_end(stats); });
    return stats;
}

void OcasPipeline::accumulate(const CycleStats& c) noexcept {
    totals_.cycle = c.cycle;
    totals_.ingested += c.ingested;
    totals_.rejected_input += c.rejected_input;
    totals_.scored += c.scored;
    totals_.actionable += c.actionable;
    totals_.logistic_used += c.logistic_used;
    totals_.fallback_used += c.fallback_used;
    totals_.directives += c.directives;
    totals_.committed += c.committed;
    totals_.rolled_back += c.rolled_back;
    totals_.rejected_dv_cap += c.rejected_dv_cap;
    totals_.rejected_budget += c.rejected_budget;
    totals_.suppressed_duplicates += c.suppressed_duplicates;
    totals_.monitored += c.monitored;
    totals_.ingest_overflow_total = c.ingest_overflow_total;
    totals_.heap_rejected_total = c.heap_rejected_total;
    totals_.cycle_latency_us += c.cycle_latency_us;
    totals_.dv_budget_remaining_mps = c.dv_budget_remaining_mps;
}

void OcasPipeline::finish() {
    observers_.notify([&](IPipelineObserver& o) { o.on_session_end(totals_); });
}

}  // namespace ocas
