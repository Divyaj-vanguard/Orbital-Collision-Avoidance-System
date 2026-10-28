#include "pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ocas {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ns(Clock::time_point since) noexcept {
    return std::chrono::duration<double, std::nano>(Clock::now() - since).count();
}

bool validate(const ConjunctionTelemetry& t, InputRejectReason& why) noexcept {
    if (!std::isfinite(t.miss_distance_m) || !std::isfinite(t.relative_speed_km_s) ||
        !std::isfinite(t.time_to_tca_sec) || t.relative_speed_km_s < 0.0) {
        why = InputRejectReason::NonFinite;
    } else if (t.miss_distance_m < 0.0) {
        why = InputRejectReason::NegativeDistance;
    } else if (t.time_to_tca_sec <= 0.0) {
        why = InputRejectReason::PastTca;
    } else {
        return true;
    }
    return false;
}

}  // namespace

bool ranks_below(const ScoredThreat& a, const ScoredThreat& b) noexcept {
    if (a.risk.actionable != b.risk.actionable) return !a.risk.actionable;
    if (a.priority != b.priority) return a.priority < b.priority;
    if (a.telemetry.time_to_tca_sec != b.telemetry.time_to_tca_sec) {
        return a.telemetry.time_to_tca_sec > b.telemetry.time_to_tca_sec;
    }
    if (a.telemetry.alert_id != b.telemetry.alert_id) return a.telemetry.alert_id > b.telemetry.alert_id;
    return a.sequence > b.sequence;
}

bool Pipeline::initialize(const PipelineConfig& cfg, Recorder* recorder, std::string& error) {
    if (cfg.max_advisories_per_cycle == 0 || cfg.max_advisories_per_cycle > kMaxStagedPerCycle) {
        error = "max_advisories_per_cycle must be in [1, " + std::to_string(kMaxStagedPerCycle) + "]";
        return false;
    }
    if (!(cfg.dv_budget_mps >= 0.0) || !(cfg.priority_model_weight >= 0.0 && cfg.priority_model_weight <= 1.0)) {
        error = "dv_budget_mps must be >= 0 and priority_model_weight in [0, 1]";
        return false;
    }
    if (!make_regime(cfg.altitude_km, regime_)) {
        error = "altitude " + std::to_string(cfg.altitude_km) + " km is not a valid orbital altitude";
        return false;
    }
    cfg_ = cfg;
    lstm_.params = cfg.lstm;
    rec_ = recorder;
    dv_budget_ = cfg.dv_budget_mps;
    inbox_.reserve(kInboxCapacity);
    heap_.reserve(kInboxCapacity);
    if (rec_) rec_->session_start(regime_, cfg.dv_budget_mps, cfg.lstm.threshold, cfg.abort_every_n, cfg.input_label);
    return true;
}

bool Pipeline::ingest(const ConjunctionTelemetry& t) {
    const bool full = inbox_.size() == kInboxCapacity;
    if (full) {
        inbox_.erase(inbox_.begin());
        ++overflows_;
    }
    inbox_.push_back(t);
    return full;
}

bool Pipeline::already_advised(std::uint32_t alert_id) const noexcept {
    for (std::size_t i = 0; i < staged_count_; ++i) {
        if (staged_[i].record.directive.alert_id == alert_id) return true;
    }
    const auto end = advised_.begin() + std::min(advised_count_, kAdvisedWindow);
    return std::find(advised_.begin(), end, alert_id) != end;
}

void Pipeline::score_inbox(CycleStats& stats) {
    const bool host_in_leo = regime_.kind == OrbitalRegime::LEO;
    for (const ConjunctionTelemetry& t : inbox_) {
        const auto start = Clock::now();
        ++stats.ingested;
        InputRejectReason why{};
        if (!validate(t, why)) {
            ++stats.rejected_input;
            if (rec_) rec_->input_rejected(t, why);
            continue;
        }
        ScoredThreat s{};
        s.telemetry = t;
        s.sequence = ++sequence_;
        s.risk = assess_risk(lstm_, t, host_in_leo);
        s.regime_risk = regime_risk(regime_, t);
        s.priority = cfg_.priority_model_weight * s.risk.score + (1.0 - cfg_.priority_model_weight) * s.regime_risk;
        heap_.push_back(s);
        std::push_heap(heap_.begin(), heap_.end(), ranks_below);
        score_ns_ += elapsed_ns(start);

        ++stats.scored;
        if (s.risk.model == RiskModel::Lstm) ++stats.model_used;
        else ++stats.fallback_used;
        if (s.risk.actionable) ++stats.actionable;
        if (rec_) rec_->threat_scored(s);
    }
    inbox_.clear();
}

void Pipeline::issue_advisories(CycleStats& stats) {
    while (stats.directives < cfg_.max_advisories_per_cycle && !heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), ranks_below);
        const ScoredThreat top = heap_.back();
        heap_.pop_back();
        if (!top.risk.actionable) break;  // heap order puts every actionable threat first
        const std::uint32_t id = top.telemetry.alert_id;
        if (already_advised(id)) {
            ++stats.suppressed_duplicates;
            continue;
        }
        LedgerRecord rec{};
        rec.cycle = cycle_;
        rec.directive = plan_evasion(regime_, top.telemetry);
        rec.risk_score = top.risk.score;
        rec.priority = top.priority;
        rec.model = top.risk.model;
        if (!rec.directive.within_regime_dv_cap) {
            rec.status = AdvisoryStatus::RejectedDvCap;
            ++stats.rejected_dv_cap;
        } else if (rec.directive.delta_v_mps > dv_budget_) {
            rec.status = AdvisoryStatus::RejectedBudget;
            ++stats.rejected_budget;
        } else {
            rec.status = AdvisoryStatus::Staged;
            staged_[staged_count_++] = Staged{rec, dv_budget_, ++staged_ordinal_};
            dv_budget_ -= rec.directive.delta_v_mps;
        }
        if (rec.status != AdvisoryStatus::Staged) advised_[advised_count_++ % kAdvisedWindow] = id;  // escalated
        ++stats.directives;
        if (rec_) rec_->advisory(rec);
    }
}

void Pipeline::acknowledge_advisories(CycleStats& stats) {
    // An abort of advisory k also undoes every advisory staged after k: they were sized
    // against a budget that included k's reservation.
    std::size_t abort_at = staged_count_;
    for (std::size_t i = 0; cfg_.abort_every_n != 0 && i < staged_count_; ++i) {
        if (staged_[i].ordinal % cfg_.abort_every_n == 0) {
            abort_at = i;
            break;
        }
    }
    if (abort_at < staged_count_) {
        const std::size_t unwound = staged_count_ - abort_at;
        dv_budget_ = staged_[abort_at].budget_before;
        for (std::size_t i = abort_at; i < staged_count_; ++i) {
            staged_[i].record.status = AdvisoryStatus::RolledBack;
            if (rec_) rec_->advisory_resolved(staged_[i].record);
        }
        stats.rolled_back += unwound;
        if (rec_) rec_->rollback(staged_[abort_at].record.directive.alert_id, unwound, dv_budget_);
    }
    for (std::size_t i = 0; i < abort_at; ++i) {
        staged_[i].record.status = AdvisoryStatus::Committed;
        if (rec_) rec_->advisory_resolved(staged_[i].record);
        advised_[advised_count_++ % kAdvisedWindow] = staged_[i].record.directive.alert_id;
        ++stats.committed;
    }
}

CycleStats Pipeline::run_cycle() {
    const auto start = Clock::now();
    CycleStats stats{};
    stats.cycle = ++cycle_;
    staged_count_ = 0;
    score_inbox(stats);
    issue_advisories(stats);
    acknowledge_advisories(stats);
    heap_.clear();  // unadvised threats are re-scored when their next CDM update arrives
    stats.ingest_overflow_total = overflows_;
    stats.dv_budget_remaining_mps = dv_budget_;
    stats.cycle_latency_us = elapsed_ns(start) / 1000.0;

    totals_.cycle = stats.cycle;
    totals_.ingested += stats.ingested;
    totals_.rejected_input += stats.rejected_input;
    totals_.scored += stats.scored;
    totals_.actionable += stats.actionable;
    totals_.model_used += stats.model_used;
    totals_.fallback_used += stats.fallback_used;
    totals_.directives += stats.directives;
    totals_.committed += stats.committed;
    totals_.rolled_back += stats.rolled_back;
    totals_.rejected_dv_cap += stats.rejected_dv_cap;
    totals_.rejected_budget += stats.rejected_budget;
    totals_.suppressed_duplicates += stats.suppressed_duplicates;
    totals_.ingest_overflow_total = stats.ingest_overflow_total;
    totals_.dv_budget_remaining_mps = stats.dv_budget_remaining_mps;

    if (rec_) rec_->cycle_end(stats);
    return stats;
}

void Pipeline::finish() {
    if (rec_) rec_->session_end(totals_);
}

}  // namespace ocas
